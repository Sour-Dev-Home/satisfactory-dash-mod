import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { checkApi, extractUses, parseSourceMap, scanSources } from "../src/check-api";
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

  it("excuses a method called on a receiver declared with one of the group's own types", () => {
    const own = "struct FOwn { bool Load() const; };\nbool FOwn::Load() const { return true; }\n";
    expect(names(`${own}void F() { FOwn Store; Store.Load(); }`)).toEqual([]);
    expect(names(`${own}void F() { const FOwn Store{P}; Store->Load(); }`)).toEqual([]);
    expect(names(`struct FOwn {};
void F(const FOwn& Ledger) { Ledger.Begin(); }`)).toEqual([]);
  });

  it("does not excuse a receiver declared with an engine or template type", () => {
    const own = "struct FOwn { bool IsSet() const; };\nbool FOwn::IsSet() const { return true; }\n";
    expect(names(`${own}void F() { TOptional<FOwn> X; X.IsSet(); }`)).toContain("IsSet");
    expect(names(`${own}void F(const FString& X) { X.IsSet(); }`)).toContain("IsSet");
  });

  it("does not excuse a name that the group declares with an own type and with an engine type", () => {
    const own = "struct FOwn { bool IsSet() const; };\nbool FOwn::IsSet() const { return true; }\n";
    expect(names(`${own}void A() { FOwn X; X.IsSet(); }\nvoid B() { TOptional<FOwn> X; X.IsSet(); }`)).toContain("IsSet");
    expect(names(`${own}void A() { FOwn X; X.IsSet(); }\nvoid B(IFoo& X) { X.IsSet(); }`)).toContain("IsSet");
  });

  it("does not excuse an engine receiver whose method name the group also defines", () => {
    const own = "struct FOwn { bool Delete() const; };\nbool FOwn::Delete() const { return true; }\n";
    expect(names(`${own}void F() { IFileManager& Files = IFileManager::Get(); Files.Delete(x); }`)).toContain("Delete");
    expect(names(`${own}void F() { Thing.Delete(); }`)).toEqual(["Delete"]);
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
    expect(await main(["run", "S9", "--out", "x"], { log: () => {}, error: (s) => void errors.push(s) })).toBe(2);
    expect(errors.join(" ")).toMatch(/S0, S1, S1b, S2, S3, S4, S5, S6, S7>/);
  });

  it("rejudge needs a bundle directory", async () => {
    const errors: string[] = [];
    expect(await main(["rejudge"], { log: () => {}, error: (s) => void errors.push(s) })).toBe(2);
    expect(errors.join(" ")).toMatch(/usage: harness rejudge <bundle dir>/);
  });
});

describe("extractUses locals", () => {
  it("treats the code's own namespaces and macro-declared test classes as local", () => {
    const uses = extractUses([
      cpp("namespace SatisDash::Test { inline int Twice(int x) { return x * 2; } }\n" +
        "IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMyTest, \"A.B\", 0)\n" +
        "bool FMyTest::RunTest(const FString& P) { return SatisDash::Test::Twice(1) > 0; }\n"),
    ]);
    const names = uses.map((u) => u.name);
    expect(names).not.toContain("SatisDash::Test::Twice");
    expect(names).not.toContain("FMyTest::RunTest");
    expect(names).toContain("IMPLEMENT_SIMPLE_AUTOMATION_TEST");
  });
});

describe("extractUses namespace excuse", () => {
  const names = (text: string) => extractUses([cpp(text)]).map((u) => u.name);
  it("does not excuse using-directives or namespace aliases", () => {
    expect(names("using namespace UE;\nvoid f() { UE::Tasks::Launch(); }\n")).toContain("UE::Tasks::Launch");
    expect(names("namespace Fs = std::filesystem;\nvoid f() { Fs::Exists(); }\n")).toContain("Fs::Exists");
  });
  it("does not excuse engine roots, even when the code defines a namespace in them", () => {
    expect(names("namespace UE::SatisDash { int X(); }\nvoid f() { UE::Tasks::Launch(); }\n")).toContain("UE::Tasks::Launch");
  });
  it("excuses only the first component of a defined namespace", () => {
    const n = names("namespace Mine::Inner { int X(); }\nvoid f() { Mine::Inner::X(); Inner::Y(); }\n");
    expect(n).not.toContain("Mine::Inner::X");
    expect(n).toContain("Inner::Y");
  });
});

describe("scanSources", () => {
  it("scans mod/ as well as spikes/, and excuses the plugin's own modules", () => {
    const root = mkdtempSync(join(tmpdir(), "scan-"));
    try {
      mkdirSync(join(root, "spikes", "S9"), { recursive: true });
      mkdirSync(join(root, "mod", "P", "Source", "A"), { recursive: true });
      mkdirSync(join(root, "mod", "P", "Source", "B"), { recursive: true });
      writeFileSync(join(root, "spikes", "S9", "x.cpp"), "void f() { FSpikeThing::Go(); }\n");
      writeFileSync(join(root, "mod", "P", "Source", "A", "A.Build.cs"),
        'PublicDependencyModuleNames.AddRange(new string[] { "Core" });\n');
      writeFileSync(join(root, "mod", "P", "Source", "B", "B.Build.cs"),
        'PublicDependencyModuleNames.AddRange(new string[] { "Core", "A" });\n');
      const files = scanSources(root);
      expect(files.map((f) => f.path)).toEqual([
        "spikes/S9/x.cpp", "mod/P/Source/A/A.Build.cs", "mod/P/Source/B/B.Build.cs",
      ]);
      const md = "| Symbol | Header | Source |\n| --- | --- | --- |\n| module:Core | Engine/Source/Runtime/Core/Core.Build.cs:1 | header only |\n";
      const names = checkApi(files, md).missing.map((u) => u.name);
      expect(names.some((n) => n.startsWith("FSpikeThing"))).toBe(true); // spikes/ is still checked
      expect(names).not.toContain("module:A"); // the plugin's own module needs no row
    } finally {
      rmSync(root, { recursive: true, force: true });
    }
  });
});
