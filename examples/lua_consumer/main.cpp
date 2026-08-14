#include <liquid/Runtime.hpp>
#include <liquid/scripting/LuaBehaviorRunner.hpp>

int main() {
    liquid::Runtime runtime;
    const liquid::BehaviorId behavior = runtime.world().create_behavior();
    liquid::scripting::LuaBehaviorRunner runner;
    const liquid::scripting::LuaExecutionResult result = runner.execute(
        runtime.world(),
        behavior,
        0,
        "local answer = 40 + 2"
    );

    return result.succeeded() ? 0 : 1;
}
