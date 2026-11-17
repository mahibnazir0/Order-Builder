# Writes the build's identity for the M5 evidence records: the commit it was built from and whether
# the tree differed from it (any change or untracked file git does not ignore). Runs at every build
# (target m5BuildIdentity) and only reads git. Without git the identity is "unknown" and dirty, which
# never counts for acceptance.
#   cmake -DSOURCE_DIR=<repo> -DOUTPUT=<file> -P m5BuildIdentity.cmake

set(commit "unknown")
set(dirty "true")
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
                  WORKING_DIRECTORY "${SOURCE_DIR}"
                  RESULT_VARIABLE revParseResult
                  OUTPUT_VARIABLE headCommit
                  OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET)
  execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain
                  WORKING_DIRECTORY "${SOURCE_DIR}"
                  RESULT_VARIABLE statusResult
                  OUTPUT_VARIABLE changes
                  ERROR_QUIET)
  if(revParseResult EQUAL 0)
    set(commit "${headCommit}")
  endif()
  if(revParseResult EQUAL 0 AND statusResult EQUAL 0 AND changes STREQUAL "")
    set(dirty "false")
  endif()
endif()
file(WRITE "${OUTPUT}" "commit=${commit}\ndirty=${dirty}\n")
