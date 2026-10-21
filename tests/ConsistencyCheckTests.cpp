// Runs tools/consistency_check.py (parameter IDs across registry, presets, help, UI layout, MIDI learn, randomizer).
// Skips (passes) when no python3 interpreter is available so minimal CI images do not fail spuriously.
#include <cstdio>
#include <cstdlib>
#include <string>

#ifndef SNARAL_SOURCE_DIR
#error "SNARAL_SOURCE_DIR must be defined (see snaral_state_pure in CMakeLists.txt)"
#endif

int main()
{
    const std::string root = SNARAL_SOURCE_DIR;
#ifdef _WIN32
    const char* py[] = { "python", "py -3" };
    const char* quiet = " >NUL 2>&1";
#else
    const char* py[] = { "python3", "python" };
    const char* quiet = " >/dev/null 2>&1";
#endif
    for (const char* exe : py)
    {
        if (std::system ((std::string (exe) + " --version" + quiet).c_str()) != 0)
            continue;
        const std::string cmd = std::string (exe) + " \"" + root + "/tools/consistency_check.py\" \"" + root + "\"";
        const int rc = std::system (cmd.c_str());
        if (rc != 0)
        {
            std::fprintf (stderr, "consistency_check.py failed (exit %d)\n", rc);
            return 1;
        }
        std::puts ("ConsistencyCheckTests passed");
        return 0;
    }
    std::puts ("SKIP: no python interpreter found");
    return 0;
}
