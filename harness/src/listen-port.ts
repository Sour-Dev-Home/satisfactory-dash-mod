/**
 * Which port a harness listener (recorder, probe stubs) binds on 127.0.0.1. Normally 0, an ephemeral port. With the docker
 * server host the container must forward those ports from its start (FORWARD_PORTS), before any listener exists, so the
 * runner config names a pool and the listeners take ports from it in turn.
 */
export function listenPortPlan(ports: readonly number[]): () => number {
  let next = 0;
  return () => (ports.length ? ports[next++ % ports.length]! : 0);
}

let plan: () => number = () => 0;

export const setListenPlan = (ports: readonly number[]): void => void (plan = listenPortPlan(ports));

/** The port for the next listener. */
export const nextListenPort = (): number => plan();
