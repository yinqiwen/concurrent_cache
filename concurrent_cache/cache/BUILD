package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "allign",
    hdrs = [
        "allign.h",
    ],
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
    name = "time",
    hdrs = [
        "time.h",
    ],
)

cc_library(
    name = "stats",
    hdrs = [
        "stats.h",
    ],
)

cc_library(
    name = "cache_handle",
    hdrs = [
        "cache_handle.h",
    ],
)

cc_library(
    name = "overflow",
    hdrs = [
        "overflow.h",
    ],
)

cc_library(
    name = "hashmap",
    # srcs = ["clock_cache_impl.cc"],
    hdrs = [
        "hashmap.h",
        "hashmap_impl.h",
    ],
    deps = [
        "//concurrent_cache/cache/detail:cache_handle",
        "//concurrent_cache/cache/detail:errcode",
        "//concurrent_cache/cache/detail:iterator",
        "//concurrent_cache/cache/detail:stats",
        "//concurrent_cache/cache/detail:ttl",
        "//concurrent_cache/common:allign",
        "//concurrent_cache/simd:simd_ops",
        "@com_google_absl//absl/container:flat_hash_map",
    ],
)

cc_library(
    name = "lru_cache",
    # srcs = ["clock_cache_impl.cc"],
    hdrs = [
        "lru_cache.h",
        "lru_cache_impl.h",
    ],
    deps = [
        "//concurrent_cache/cache/detail:cache_handle",
        "//concurrent_cache/cache/detail:errcode",
        "//concurrent_cache/cache/detail:hazptr",
        "//concurrent_cache/cache/detail:iterator",
        "//concurrent_cache/cache/detail:stats",
        "//concurrent_cache/cache/detail:ttl",
        "//concurrent_cache/common:allign",
        "//concurrent_cache/common:time",
        "//concurrent_cache/simd:simd_ops",
        "@com_google_absl//absl/container:flat_hash_map",
    ],
)

cc_test(
    name = "lru_cache_test",
    size = "small",
    srcs = ["lru_cache_test.cc"],
    linkopts = [
        "-L/usr/local/lib64",
        "-lfmt",
        "-L/usr/local/lib",
        "-lfolly",
        "-lunwind",
        "-ldouble-conversion",
        "-lglog",
        "-lgflags",
        "-ldl",
        "-liberty",
    ],
    linkstatic = True,
    deps = [
        ":lru_cache",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "hashmap_test",
    size = "small",
    srcs = ["hashmap_test.cc"],
    linkopts = [
        "-L/usr/local/lib64",
        "-lfmt",
        "-L/usr/local/lib",
        "-lfolly",
        "-lunwind",
        "-ldouble-conversion",
        "-lglog",
        "-lgflags",
        "-ldl",
        "-liberty",
    ],
    linkstatic = True,
    deps = [
        ":hashmap",
        "@com_google_googletest//:gtest_main",
    ],
)
