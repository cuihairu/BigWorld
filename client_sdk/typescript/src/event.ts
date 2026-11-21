/**
 * kbengine-aligned event bus. TypeScript port of the KBEngine.Event singleton
 * in kbengine's JS client plugin (kbe/res/sdk_templates/client/js/kbengine.js,
 * `KBEngine.Event`). Semantics mirror the original:
 *
 *   register(evtName, classinst, strCallback)
 *     - strCallback is the NAME of a method on classinst; the handler is
 *       looked up once and stored with its owner so `fire` can call it with
 *       the owner as `this` (kbengine does `callbackfn.apply(classinst, ars)`).
 *   deregister(evtName, classinst)
 *     - removes every handler owned by classinst for that event (and drains
 *       any queued fired events for it).
 *   deregisterAll(classinst)
 *   fire(evtName, ...args)
 *     - dispatches to all registered handlers; while paused, fired events are
 *       queued and replayed on resume.
 *   pause() / resume() / clear()
 *
 * Deviation from kbengine (deliberate, fail-fast): kbengine logs an error and
 * returns when the named callback is not a function; here we throw an Error so
 * a mis-typed registration surfaces immediately. Documented in README.
 */

type Handler = (...args: unknown[]) => void;

interface EventInfo {
  owner: unknown;
  callback: Handler;
}

interface FiredEvent {
  name: string;
  info: EventInfo;
  args: unknown[];
}

export class EventBus {
  private events = new Map<string, EventInfo[]>();
  private paused = false;
  private fired: FiredEvent[] = [];

  /** Register the method strCallback on owner for the named event. */
  register(evtName: string, owner: unknown, strCallback: string): void {
    if (typeof strCallback !== "string" || strCallback.length === 0) {
      throw new Error(`Event.register: strCallback must be a method name, got ${String(strCallback)}`);
    }
    if (owner === null || owner === undefined) {
      throw new Error(`Event.register: owner is ${String(owner)}`);
    }

    const callback = (owner as Record<string, unknown>)[strCallback];
    if (typeof callback !== "function") {
      throw new Error(`Event.register: ${strCallback} is not a function on owner`);
    }

    let list = this.events.get(evtName);
    if (list === undefined) {
      list = [];
      this.events.set(evtName, list);
    }
    list.push({ owner, callback: callback as Handler });
  }

  /** Remove every handler owned by owner for the named event. */
  deregister(evtName: string, owner: unknown): void {
    const list = this.events.get(evtName);
    if (list !== undefined) {
      for (let i = list.length - 1; i >= 0; i--) {
        if (list[i].owner === owner) {
          list.splice(i, 1);
        }
      }
      if (list.length === 0) {
        this.events.delete(evtName);
      }
    }
    this.removeFired(evtName, owner);
  }

  /** Remove every handler owned by owner across all events. */
  deregisterAll(owner: unknown): void {
    for (const name of [...this.events.keys()]) {
      this.deregister(name, owner);
    }
  }

  /** Dispatch the named event to every registered handler. */
  fire(evtName: string, ...args: unknown[]): void {
    const list = this.events.get(evtName);
    if (list === undefined) {
      return;
    }
    for (const info of list) {
      if (this.paused) {
        this.fired.push({ name: evtName, info, args });
      } else {
        info.callback.apply(info.owner, args);
      }
    }
  }

  pause(): void {
    this.paused = true;
  }

  resume(): void {
    this.paused = false;
    const queued = this.fired;
    this.fired = [];
    for (const evt of queued) {
      evt.info.callback.apply(evt.info.owner, evt.args);
    }
  }

  /** Remove all registrations and queued events. */
  clear(): void {
    this.events.clear();
    this.fired = [];
  }

  get size(): number {
    return this.events.size;
  }

  private removeFired(evtName: string, owner: unknown): void {
    this.fired = this.fired.filter(
      (evt) => !(evt.name === evtName && evt.info.owner === owner)
    );
  }
}

/** Shared singleton, mirroring KBEngine.Event = new KBEngine.Event(). */
export const Event = new EventBus();
