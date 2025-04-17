package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "errcode",
    hdrs = [
        "errcode.h",
    ],
)

cc_library(
    name = "ttl",
    hdrs = [
        "ttl.h",
    ],
)

cc_library(
    name = "stats",
    hdrs = [
        "stats.h",
    ],
)

cc_library(
    name = "hazptr",
    srcs = ["hazptr.cc"],
    hdrs = [
        "hazptr.h",
    ],
)

cc_library(
    name = "cache_handle",
    hdrs = [
        "cache_handle.h",
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
        "//concurrent_cache/common:pool",
    ],
)

cc_library(
    name = "iterator",
    hdrs = [
        "iterator.h",
    ],
    deps = [":node"],
)
