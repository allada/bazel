load("@rules_java//java:defs.bzl", "java_library", "java_test")

filegroup(
    name = "srcs",
    testonly = 0,
    srcs = glob(["**"]) + [
        "//src/test/java/com/google/devtools/build/lib/blackbox/tests/workspace:srcs",
    ],
    visibility = ["//src/test/java/com/google/devtools/build/lib/blackbox:__subpackages__"],
)

java_library(
    name = "common_deps",
    testonly = 1,
    visibility = ["//src/test/java/com/google/devtools/build/lib/blackbox/tests:__subpackages__"],
    exports = [
        "//src/test/java/com/google/devtools/build/lib/blackbox:base_deps",
        "//src/test/java/com/google/devtools/build/lib/blackbox:test_deps",
        "//src/test/java/com/google/devtools/build/lib/blackbox/bazel:context",
        "//src/test/java/com/google/devtools/build/lib/blackbox/framework",
        "//src/test/java/com/google/devtools/build/lib/blackbox/junit:test_base",
    ],
)

java_test(
    name = "ManagedDirectoriesBlackBoxTest",
    timeout = "moderate",
    srcs = ["manageddirs/ManagedDirectoriesBlackBoxTest.java"],
    resources = [
        "manageddirs/BUILD.test",
        "manageddirs/WORKSPACE.test",
        "manageddirs/bazelignore.test",
        "manageddirs/package.json",
        "manageddirs/test_rule.bzl",
        "manageddirs/use_node_modules.bzl",
    ],
    tags = ["black_box_test"],
    deps = [
        ":common_deps",
    ],
)

java_test(
    name = "PythonBlackBoxTest",
    timeout = "moderate",
    srcs = ["PythonBlackBoxTest.java"],
    tags = ["black_box_test"],
    deps = [
        ":common_deps",
        "//src/test/java/com/google/devtools/build/lib/blackbox/bazel:python_tools",
    ],
)

java_test(
    name = "NinjaBlackBoxTest",
    timeout = "moderate",
    srcs = ["NinjaBlackBoxTest.java"],
    tags = ["black_box_test"],
    deps = [
        ":common_deps",
        "//src/main/java/com/google/devtools/build/lib:bazel-ninja",
    ],
)

test_suite(
    name = "black_box_tests",
    tags = ["black_box_test"],
    tests = [
        "ManagedDirectoriesBlackBoxTest",
        "PythonBlackBoxTest",
        "//src/test/java/com/google/devtools/build/lib/blackbox/framework:framework_tests",
        "//src/test/java/com/google/devtools/build/lib/blackbox/junit:TimeoutTestWatcherTest",
        "//src/test/java/com/google/devtools/build/lib/blackbox/tests/workspace:ws_black_box_tests",
    ],
)

test_suite(
    name = "blackbox_windows_tests",
    tests = [
        ":black_box_tests",
    ],
    visibility = ["//src/test/java/com/google/devtools/build/lib:__pkg__"],
)
