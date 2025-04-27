package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "hazptr",
    srcs = ["hazptr.cc"],
    hdrs = [
        "hazptr.h",
    ],
)

cc_library(
    name = "node",
    srcs = ["node.cc"],
    hdrs = [
        "node.h",
    ],
    deps = [
        ":hazptr",
        "//concurrent_cache/cache:options",
        "//concurrent_cache/common:pool",
        "//concurrent_cache/common:time",
        "//concurrent_cache/simd:simd_ops",
    ],
)

cc_library(
    name = "iterator",
    hdrs = [
        "iterator.h",
    ],
    deps = [":node"],
)
