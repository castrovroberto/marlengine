#pragma once

#include "Marle/Core.h"
#include "ComponentRegistry.h"
#include <iosfwd>
#include <string>
#include <vector>

namespace Marle {

    // The command line behind `marle-scene`, for checking and tidying scene files without a window
    // or GL context. `args` are the arguments after the program name. A game with components of its
    // own builds its own tool by calling this with its registry.
    //
    //   validate <file|dir>...             load each scene file and report every problem; a directory
    //                                      means every .json file in it, except *.schema.json
    //   format [--check] [--drop-comments] [--schema <path>] <file|dir>...
    //                                      rewrite scene files in canonical form (an existing $schema
    //                                      key is kept). Refuses a file that has problems, since
    //                                      formatting would drop the parts that failed to load, and
    //                                      leaves a file with comments alone unless --drop-comments
    //                                      says to lose them. --check only reports what would change
    //   schema [--output <file> | --check <file>]
    //                                      print the JSON Schema for scene files, write it to a file,
    //                                      or check that a file is up to date
    //   simulate <file> [--steps N] [--recording <file>] [--seed N] [--expect <checksum>]
    //                                      load a scene (refused if it has any problem) and run it headless for
    //                                      N fixed steps, by default the recording's length, else 600. Input comes
    //                                      from the recording, else there is none. Prints what changed (field by
    //                                      field, first 40) and the final checksum. --seed replaces the scene's
    //                                      seed (and then the recording's seed is not compared). --expect makes it
    //                                      a check: exit 1 if the checksum differs, as it does when a whole
    //                                      recording that carries a checksum does not reproduce it
    //   help
    //
    // Results and problems go to `out`; mistakes in the command line go to `err`.
    // Returns 0 when everything is fine, 1 when problems were found (or, with --check, files differ),
    // and 2 for a command line that cannot be used.
    EXPORT int RunSceneTool(const std::vector<std::string>& args, const ComponentRegistry& registry, std::ostream& out, std::ostream& err);

}
