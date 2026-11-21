import { describe, expect, it } from "vitest";
import { EventBus } from "../src/index.js";

/** Event bus semantics against kbengine's KBEngine.Event (kbengine.js):
 * register(evtName, owner, methodName) → fire(evtName, ...args) invokes the
 * owner's method with the owner as `this`. */

class Listener {
  received: string[] = [];

  onLogin(accountName: string, password: string): void {
    this.received.push(`${accountName}:${password}`);
  }
}

describe("EventBus", () => {
  it("fire invokes the owner method with owner as this", () => {
    const bus = new EventBus();
    const listener = new Listener();
    bus.register("login", listener, "onLogin");

    bus.fire("login", "alice", "secret");

    expect(listener.received).toEqual(["alice:secret"]);
  });

  it("fires every registered handler in registration order", () => {
    const bus = new EventBus();
    const a = new Listener();
    const b = new Listener();
    bus.register("login", a, "onLogin");
    bus.register("login", b, "onLogin");

    bus.fire("login", "x", "y");

    expect(a.received).toEqual(["x:y"]);
    expect(b.received).toEqual(["x:y"]);
  });

  it("fire with no handlers is a no-op", () => {
    const bus = new EventBus();
    expect(() => bus.fire("nobody")).not.toThrow();
  });

  it("fire with no extra args calls the method with none", () => {
    const bus = new EventBus();
    const events: unknown[][] = [];
    const owner = {
      onPing: function (this: unknown, ...args: unknown[]) {
        events.push(args);
      },
    };
    bus.register("ping", owner, "onPing");
    bus.fire("ping");
    expect(events).toEqual([[]]);
  });

  it("deregister removes only that owner's handlers", () => {
    const bus = new EventBus();
    const a = new Listener();
    const b = new Listener();
    bus.register("login", a, "onLogin");
    bus.register("login", b, "onLogin");

    bus.deregister("login", a);
    bus.fire("login", "m", "n");

    expect(a.received).toEqual([]);
    expect(b.received).toEqual(["m:n"]);
  });

  it("deregisterAll removes the owner across every event", () => {
    const bus = new EventBus();
    const a = new Listener();
    bus.register("login", a, "onLogin");
    bus.register("logout", a, "onLogin");

    bus.deregisterAll(a);
    bus.fire("login", "a", "b");
    bus.fire("logout", "a", "b");

    expect(a.received).toEqual([]);
  });

  it("pause queues fired events, resume replays them", () => {
    const bus = new EventBus();
    const listener = new Listener();
    bus.register("login", listener, "onLogin");

    bus.pause();
    bus.fire("login", "queued", "1");
    expect(listener.received).toEqual([]); // queued, not yet dispatched

    bus.resume();
    expect(listener.received).toEqual(["queued:1"]);
  });

  it("clear removes all registrations and queued events", () => {
    const bus = new EventBus();
    const listener = new Listener();
    bus.register("login", listener, "onLogin");

    bus.pause();
    bus.fire("login", "a", "b");
    bus.clear();

    expect(bus.size).toBe(0);
    bus.resume();
    expect(listener.received).toEqual([]);
  });

  it("deregister drains queued fired events for that owner", () => {
    const bus = new EventBus();
    const a = new Listener();
    const b = new Listener();
    bus.register("login", a, "onLogin");
    bus.register("login", b, "onLogin");

    bus.pause();
    bus.fire("login", "a", "b");
    bus.deregister("login", a);

    bus.resume();
    expect(a.received).toEqual([]); // queued event for a was dropped
    expect(b.received).toEqual(["a:b"]);
  });

  it("register throws when the named callback is not a method", () => {
    const bus = new EventBus();
    expect(() => bus.register("login", { notAMethod: 1 }, "nope")).toThrow(
      /not a function/
    );
  });
});
