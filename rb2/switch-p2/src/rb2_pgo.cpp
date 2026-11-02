// RB2 (Switch): writes the profile of an RB2_PGO=GEN build to sdmc:/rb2-pgo (after Prototype 2 NX
// switch_pgo.cpp, penettrator, GPL-3.0). Every 3 minutes a helper thread dumps the counters (libgcov merges them
// into the files already there) and resets them, so any play session of 3+ minutes leaves a usable profile even
// though the game is never exited cleanly. The build is slower than normal: it is only for recording.
#if defined(RB2_PGO_GEN) && RB2_PGO_GEN
#include <chrono>
#include <thread>
#include <sys/stat.h>

#include <rex/logging.h>

extern "C" void __gcov_dump(void);
extern "C" void __gcov_reset(void);

namespace {
struct PgoDumper {
  PgoDumper() {
    new std::thread([] {
      for (int i = 1;; ++i) {
        std::this_thread::sleep_for(std::chrono::minutes(3));
        mkdir("sdmc:/rb2-pgo", 0777);  // libgcov does not create directories here
        const auto begin = std::chrono::steady_clock::now();
        __gcov_dump();
        __gcov_reset();
        REXLOG_WARN("[rb2-pgo] profile dump {} written to sdmc:/rb2-pgo in {} ms", i,
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin)
                        .count());
      }
    });
  }
};
PgoDumper g_dumper;
}  // namespace
#endif
