package(default_visibility = ["//visibility:public"])

cc_library(
    name = "benchmark_libs",
    hdrs = [
        "cache.h",
        "key_generator.h",
    ],
    deps = [
        "//concurrent_cache/cache",
        "//concurrent_cache/cache:hashmap",
    ],
)

cc_binary(
    name = "cache_benchmark",
    srcs = ["cache_benchmark.cc"],
    linkopts = [
        "-L/usr/local/lib64",
        "-lfmt",
        "-L/usr/local/lib",
        "-lfolly",
        "-lfmt",
        "-lunwind",
        "-ldouble-conversion",
        "-lgflags",
        "-lglog",
        "-ldl",
        "-liberty",
    ],
    linkstatic = True,
    malloc = "@com_github_jemalloc//:jemalloc",
    deps = [
        ":benchmark_libs",
    ],
)
