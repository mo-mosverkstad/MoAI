cmake_minimum_required(VERSION 3.16)
project(moai LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTS "Build tests" ON)
option(USE_TORCH "Build with libtorch for neural encoder" OFF)

add_library(moai_lib
    src/common/file_utils.cpp
    src/common/varint.cpp
    src/common/config.cpp
    src/common/vocab_loader.cpp
    src/common/rules_loader.cpp

    src/storage/segment_writer.cpp
    src/storage/segment_reader.cpp
    src/storage/manifest.cpp
    src/storage/wal.cpp

    src/inverted/tokenizer.cpp
    src/inverted/index_builder.cpp
    src/inverted/index_reader.cpp
    src/inverted/bm25.cpp
    src/inverted/search_engine.cpp
    src/inverted/query_parser.cpp
    src/inverted/phrase_matcher.cpp

    src/hnsw/hnsw_index.cpp
    src/hnsw/hnsw_node.cpp

    src/embedding/vocab.cpp
    src/embedding/embedding_model.cpp

    src/hybrid/hybrid_builder.cpp
    src/hybrid/hybrid_search.cpp

    src/summarizer/summarizer.cpp

    src/query/query_analyzer.cpp
    src/chunk/chunker.cpp
    src/answer/answer_synthesizer.cpp
    src/answer/answer_scope.cpp
    src/answer/answer_compressor.cpp
    src/answer/answer_validator.cpp
    src/answer/evidence_normalizer.cpp
    src/answer/question_planner.cpp
    src/answer/self_ask.cpp
    src/conversation/conversation_state.cpp

    src/pipeline/pipeline.cpp

    src/profiling/profiler.cpp

    src/tools/sandbox.cpp
)

target_include_directories(moai_lib PUBLIC src)

add_executable(moai src/cli/main.cpp src/cli/commands.cpp)
target_link_libraries(moai PRIVATE moai_lib)

if(USE_TORCH)
    find_package(Torch REQUIRED)

    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)
    endif()

    add_library(moai_encoder
        src/encoder/sentence_encoder.cpp
        src/encoder/encoder_trainer.cpp
        src/query/neural_query_analyzer.cpp
    )
    target_include_directories(moai_encoder PUBLIC src)
    target_link_libraries(moai_encoder PUBLIC "${TORCH_LIBRARIES}" moai_lib)
    target_compile_definitions(moai_encoder PUBLIC HAS_TORCH=1)

    add_executable(train_encoder src/encoder/train_main.cpp)
    target_link_libraries(train_encoder PRIVATE moai_encoder)

    target_link_libraries(moai PRIVATE moai_encoder)
    target_compile_definitions(moai PRIVATE HAS_TORCH=1)
endif()

if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    add_executable(moai_tests
        tests/test_varint.cpp
        tests/test_tokenizer.cpp
        tests/test_query_parser.cpp
        tests/test_segment_io.cpp
        tests/test_bm25.cpp
        tests/test_search_engine.cpp
        tests/test_hnsw.cpp
        tests/test_hybrid.cpp
    )
    target_link_libraries(moai_tests PRIVATE moai_lib GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(moai_tests)
endif()
