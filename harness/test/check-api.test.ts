import { describe, expect, it } from "vitest";
import { checkApi, extractUses, parseSourceMap } from "../src/check-api";
import { main } from "../src/cli";

const HEADER = "| Symbol | Header | Source |\n| --- | --- | --- |\n";
const cpp = (text: string, path = "spikes/S1/Source/SatisModS1/Private/SatisModS1.cpp") => ({ path, text });

describe("extractUses", () => {
  it("finds includes, qualified names, methods, types and macros with their lines", () => {
    const uses = extractUses([
      cpp('#include "HttpModule.h"\nauto R = FHttpModule::Get().CreateRequest();\nTSharedRef<IHttpRequest> Q;\nUE_LOG(LogTemp, Display, TEXT("x"));'),
    ]);
    const names = uses.map((u) => `${u.kind}:${u.name}@${u.line}`).sort();
    expect(names).toEqual([
      "include:HttpModule.h@1",
      "method:CreateRequest@2",
      "symbol:FHttpModule::Get@2",
      "symbol:IHttpRequest@3",
      "symbol:TEXT@4",
      "symbol:TSharedRef@3",
      "symbol:UE_LOG@4",
    ]);
  });

  it("ignores comments and string contents", () => {
    const uses = extractUses([cpp('// FCommented::Thing()\n/* UOther */\nconst char* S = "FNotAType::X()";')]);
    expect(uses).toEqual([]);
  });

  it("treats the spike's own classes, methods, defines and headers as local", () => {
    const header = {
      path: "spikes/S0/Source/SatisModS0/Public/SatisModS0.h",
      text: "#pragma once\nclass SATISMODS0_API FSatisModS0Module : public IModuleInterface\n{\n};",
    };
    const source = cpp(
      '#include "SatisModS0.h"\n#include "SatisModS0.generated.h"\n#define SATIS_LINE(x) x\nvoid FSatisModS0Module::StartupModule() { this->Helper(); SATIS_LINE(1); }\nvoid FSatisModS0Module::Helper() {}',
      "spikes/S0/Source/SatisModS0/Private/SatisModS0.cpp",
    );
    const names = extractUses([header, source]).map((u) => u.name);
    expect(names).toEqual(["IModuleInterface"]);
  });

  it("does not treat a forward declaration as local", () => {
    const names = [...new Set(extractUses([cpp("class IHttpRequest;\nTSharedPtr<IHttpRequest> R;")]).map((u) => u.name))].sort();
    expect(names).toEqual(["IHttpRequest", "TSharedPtr"]);
  });

  it("reads module dependencies from Build.cs files only", () => {
    const uses = extractUses([
      {
        path: "spikes/S1/Source/SatisModS1/SatisModS1.Build.cs",
        text: 'PublicDependencyModuleNames.AddRange(new string[] { "Core",\n "HTTP" });\nPrivateDependencyModuleNames.Add("Json");',
      },
    ]);
    expect(uses.map((u) => `${u.name}@${u.line}`)).toEqual(["module:Core@1", "module:HTTP@2", "module:Json@3"]);
  });
});

describe("extractUses: evasions the final review found", () => {
  const names = (text: string, path?: string) => [...new Set(extractUses([cpp(text, path)]).map((u) => u.name))].sort();

  it("requires rows for unqualified calls, including templated ones", () => {
    expect(names("UWorld* W = GetWorld();\nAActor* A = Cast<AActor>(Obj);\nUObject* O = NewObject<UObject>();")).toEqual(
      ["AActor", "Cast", "GetWorld", "NewObject", "UObject", "UWorld"],
    );
  });

  it("catches templated method calls", () => {
    expect(names("X->GetSubsystem<UMine>();")).toEqual(["GetSubsystem", "UMine"]);
  });

  it("catches G* engine globals", () => {
    expect(names("GEngine->AddOnScreenDebugMessage(1);")).toEqual(["AddOnScreenDebugMessage", "GEngine"]);
  });

  it("joins a qualifier split across lines and keeps the first line", () => {
    const uses = extractUses([cpp("auto M = FHttpModule::\n  Get();")]);
    expect(uses.map((u) => `${u.name}@${u.line}`)).toEqual(["FHttpModule::Get@1"]);
  });

  it("does not treat a digit separator as a char literal", () => {
    expect(names("int N = 1'000; FHttpModule::Get();")).toEqual(["FHttpModule::Get"]);
  });

  it("keeps the whole nested qualifier", () => {
    expect(names("UE::Tasks::Launch(X);")).toEqual(["UE::Tasks::Launch"]);
  });

  it("excuses a local method only when called on this", () => {
    const local = "class FLocalThing {};\nvoid FLocalThing::ProcessRequest() {}\n";
    expect(names(`${local}this->ProcessRequest();`)).toEqual([]);
    expect(names(`${local}Req->ProcessRequest();`)).toEqual(["ProcessRequest"]);
  });

  it("excuses a function the spike defines or declares itself, but not a call in a return", () => {
    expect(names("static void Helper() {}\nvoid Run() { Helper(); }")).toEqual([]);
    expect(names("virtual void StartupModule() override;\n", "spikes/S1/Source/SatisModS1/Public/SatisModS1.h")).toEqual([]);
    expect(names("bool F() { return IsRunningDedicatedServer(); }")).toEqual(["IsRunningDedicatedServer"]);
  });

  it("scans API use inside a #define body", () => {
    expect(names("#define SATIS_LOG(x) UE_LOG(LogTemp, Display, x)")).toEqual(["UE_LOG"]);
  });

  it("scans .inl, .cc and .cxx files", () => {
    expect(names("FHttpModule::Get();", "spikes/S1/Source/X.inl")).toEqual(["FHttpModule::Get"]);
    expect(names("FHttpModule::Get();", "spikes/S1/Source/X.cxx")).toEqual(["FHttpModule::Get"]);
  });

  it("reads new[] module lists and every *ModuleNames list", () => {
    const uses = extractUses([
      { path: "spikes/S1/Source/X.Build.cs", text: 'PublicDependencyModuleNames.AddRange(new[] { "HTTP" });\nDynamicallyLoadedModuleNames.Add("Json");' },
    ]);
    expect(uses.map((u) => u.name)).toEqual(["module:HTTP", "module:Json"]);
  });

  it("reads plugin dependencies from the .uplugin", () => {
    const uses = extractUses([
      { path: "spikes/S1/SatisModS1.uplugin", text: '{\n  "Plugins": [\n    { "Name": "SML", "Enabled": true }\n  ]\n}' },
    ]);
    expect(uses.map((u) => `${u.name}@${u.line}`)).toEqual(["plugin:SML@3"]);
  });
});

describe("parseSourceMap", () => {
  it("reads rows and strips backticks", () => {
    const { rows, badRows } = parseSourceMap(`${HEADER}| \`FHttpModule::Get\` | \`Engine/Source/Runtime/Online/HTTP/Public/HttpModule.h:30\` | ue-http-timer-json-2026-09-27.md |\n`);
    expect(badRows).toEqual([]);
    expect(rows.get("FHttpModule::Get")).toEqual({
      header: "Engine/Source/Runtime/Online/HTTP/Public/HttpModule.h:30",
      source: "ue-http-timer-json-2026-09-27.md",
    });
  });

  it("flags a header without a line, an empty source and a duplicate", () => {
    const { badRows } = parseSourceMap(`${HEADER}| A | a.h | x.md |\n| B | b.h:3 |  |\n| C | c.h:1 | x.md |\n| C | c.h:2 | x.md |\n`);
    expect(badRows).toHaveLength(3);
    expect(badRows[0]).toMatch(/line 3: .*path:line/);
  });
});

describe("checkApi", () => {
  const file = cpp('#include "HttpModule.h"\nauto R = FHttpModule::Get().CreateRequest();');

  it("reports every use without a row", () => {
    expect(checkApi([file], HEADER).missing.map((u) => u.name)).toEqual(["HttpModule.h", "FHttpModule::Get", "CreateRequest"]);
  });

  it("passes when each use has a row, and a method is covered by any Class::Method row", () => {
    const md = `${HEADER}| HttpModule.h | Engine/x/HttpModule.h:1 | ue.md |\n| FHttpModule::Get | Engine/x/HttpModule.h:30 | ue.md |\n| FHttpModule::CreateRequest | Engine/x/HttpModule.h:40 | ue.md |\n`;
    expect(checkApi([file], md)).toEqual({ missing: [], badRows: [] });
  });
});

describe("cli check-api", () => {
  // Reads the working tree on purpose: in CI's clean checkout it is the guard that every committed spike's API use
  // has a row. Locally, an untracked spike without rows fails it too.
  it("passes on this repository's spikes", async () => {
    expect(await main(["check-api"], { log: () => {}, error: () => {} })).toBe(0);
  });

  it("exits 2 on an unknown command", async () => {
    expect(await main(["nope"], { log: () => {}, error: () => {} })).toBe(2);
  });

  it("run refuses a spike this harness version does not have", async () => {
    const errors: string[] = [];
    expect(await main(["run", "S3", "--out", "x"], { log: () => {}, error: (s) => void errors.push(s) })).toBe(2);
    expect(errors.join(" ")).toMatch(/S0, S1, S1b, S2/);
  });
});
