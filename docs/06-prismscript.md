# 6. PrismScript syntax and runtime

Native C++20 lexer → recursive-descent parser → AST → tree-walking interpreter. This is a **prototype interpreter**, not C# and not .NET NativeAOT/JIT. It runs on the Android device and on a host for tests.

```prism
// Hello world
print("Hello, Prism!");
```

```prism
// Proposed touch-to-player API — demonstrates future host bridge.
// Touch.axis and Entity.position are NOT yet registered by Android host.
class TouchPlayer {
    var speed = 5;
    func update(dt) {
        let axis = Touch.axis("horizontal");
        Entity.move(this, Vec2(axis * this.speed * dt, 0));
        if (Touch.pressed("jump")) { Entity.impulse(this, Vec2(0, 6)); }
    }
}
```

**Executable version in current VM:**

```prism
class Player {
    var x = 0;
    var speed = 5;
    func update(dt, touchAxis) { this.x += touchAxis * this.speed * dt; }
}
var hero = new Player();
hero.update(0.016, 1);
print(hero.x);  // 0.08
```

Supported: `let`/`var`, `func`, `class`/`extends`, `if`/`else`, `while`, C-style `for` and `for x in`, return/break/continue, lambdas `|x| x*2`, array/map literals, `new`, `this`, math/string/list primitives. `using`/`import` parse but **do not yet load modules**. No static type checking or code completion. `define_native` is the safe native API entry point; add permission guards and capability checks before exposing platform services to scripts.
