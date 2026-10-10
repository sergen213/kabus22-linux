# Zero-initialize dSolveLCP's state[] in the pinned ODE source, on every configure.
# Keep the official archive unchanged. Upstream allocates state[] uninitialized, and
# swapProblem() swaps entries not yet assigned: an invalid bool load (UBSan). Every
# entry is assigned before it is read as a value, so starting false changes no result.
set(_kabus_lcp_file "${kabus_ode_SOURCE_DIR}/ode/src/lcp.cpp")
set(_kabus_lcp_original_sha256 "76b3771a02736a29fa552ec49a163953420eb98aa98d8a3cbe0da21f5ed9d03c")
set(_kabus_lcp_modified_sha256 "3467227bf68f38e2ea72785006545a998672952994aca6158492018da9c5ddfa")
file(SHA256 "${_kabus_lcp_file}" _kabus_lcp_actual_sha256)
if(NOT _kabus_lcp_actual_sha256 STREQUAL _kabus_lcp_original_sha256
   AND NOT _kabus_lcp_actual_sha256 STREQUAL _kabus_lcp_modified_sha256)
    message(FATAL_ERROR "Unexpected ODE lcp source SHA256: ${_kabus_lcp_actual_sha256}")
endif()
if(_kabus_lcp_actual_sha256 STREQUAL _kabus_lcp_original_sha256)
    # The upstream file is uniformly CRLF; file(READ) drops the CRs, so restore them
    # after the insertion. The hash check below proves the exact byte result.
    file(READ "${_kabus_lcp_file}" _kabus_lcp_source)
    set(_kabus_lcp_needle "bool *state = memarena->AllocateArray<bool> (n);")
    string(FIND "${_kabus_lcp_source}" "${_kabus_lcp_needle}" _kabus_lcp_at)
    if(_kabus_lcp_at LESS 0)
        message(FATAL_ERROR "Pinned ODE dSolveLCP state allocation not found")
    endif()
    string(REPLACE "${_kabus_lcp_needle}"
        "${_kabus_lcp_needle}\n    // Kabus22: swapProblem() swaps state[] entries that are not assigned yet; start them false.\n    for (unsigned kabus_i = 0; kabus_i != n; ++kabus_i) state[kabus_i] = false;"
        _kabus_lcp_source "${_kabus_lcp_source}")
    string(ASCII 13 _kabus_cr)
    string(REPLACE "\n" "${_kabus_cr}\n" _kabus_lcp_source "${_kabus_lcp_source}")
    string(SHA256 _kabus_lcp_result_sha256 "${_kabus_lcp_source}")
    if(NOT _kabus_lcp_result_sha256 STREQUAL _kabus_lcp_modified_sha256)
        message(FATAL_ERROR "ODE lcp state recipe/hash mismatch: ${_kabus_lcp_result_sha256}")
    endif()
    file(WRITE "${_kabus_lcp_file}" "${_kabus_lcp_source}")
    file(SHA256 "${_kabus_lcp_file}" _kabus_lcp_actual_sha256)
    if(NOT _kabus_lcp_actual_sha256 STREQUAL _kabus_lcp_modified_sha256)
        message(FATAL_ERROR "Written ODE lcp source SHA256 mismatch: ${_kabus_lcp_actual_sha256}")
    endif()
endif()
message(STATUS "Kabus ODE lcp source SHA256: ${_kabus_lcp_modified_sha256}")
