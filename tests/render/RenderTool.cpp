// The null check's render tool (docs/performance.md, "Null check"), run by scripts/check.sh null for two builds of the
// Engine, which compares what they rendered:
//
//   comp1_render render <dir>            writes every scene at every sample rate into <dir>, as raw interleaved 32-bit
//                                        floats named <scene>-<rate>.raw
//   comp1_render compare <dir> <ref dir> prints each scene's worst deviation in dBFS, or "exact", and fails above the
//                                        bar (-120 dBFS), on a length that differs, or on a scene in <ref dir> missing
//                                        from <dir>; a scene only in <dir> is "new" and passes
//
// Linked against the Engine only.

#include "Compare.h"
#include "RenderScenes.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

using namespace comp1;
namespace fs = std::filesystem;

namespace
{

// The bar: a deviation under it is far below anything audible, and above the last-bit differences a change that only
// reorders arithmetic makes.
constexpr double barDb = -120.0;

std::string fileName (const std::string& scene, double sampleRate)
{
    return scene + "-" + std::to_string (static_cast<int> (sampleRate)) + ".raw";
}

bool write (const fs::path& file, const std::vector<float>& samples)
{
    std::ofstream out (file, std::ios::binary);
    out.write (reinterpret_cast<const char*> (samples.data()), static_cast<std::streamsize> (samples.size() * sizeof (float)));
    return static_cast<bool> (out);
}

std::vector<float> read (const fs::path& file)
{
    std::ifstream in (file, std::ios::binary);
    const std::vector<char> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    std::vector<float> samples (bytes.size() / sizeof (float));
    std::copy (bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t> (samples.size() * sizeof (float)), reinterpret_cast<char*> (samples.data()));
    return samples;
}

int render (const fs::path& dir)
{
    fs::create_directories (dir);
    int written = 0;
    for (const auto& scene : test::renderScenes())
        for (const double sampleRate : test::renderSampleRates)
        {
            const auto file = dir / fileName (scene.name, sampleRate);
            if (! write (file, test::render (scene, sampleRate)))
            {
                std::fprintf (stderr, "comp1_render: couldn't write %s\n", file.string().c_str());
                return 1;
            }
            ++written;
        }
    std::printf ("%d renders in %s\n", written, dir.string().c_str());
    return 0;
}

int compare (const fs::path& dir, const fs::path& refDir)
{
    bool failed = false;
    std::set<std::string> seen;
    for (const auto& scene : test::renderScenes())
        for (const double sampleRate : test::renderSampleRates)
        {
            const auto name = fileName (scene.name, sampleRate);
            seen.insert (name);
            std::printf ("%-32s %6.1f kHz  ", scene.name.c_str(), sampleRate / 1000.0);
            if (! fs::exists (refDir / name))
            {
                std::printf ("new\n");
                continue;
            }
            const auto comparison = test::compare (read (dir / name), read (refDir / name));
            if (! comparison.sameLength)
                std::printf ("FAIL: lengths differ\n");
            else if (comparison.exact)
                std::printf ("exact\n");
            else
                std::printf ("%7.1f dBFS%s\n", comparison.worstDb, comparison.passes (barDb) ? "" : "  FAIL: above the bar");
            failed = failed || ! comparison.passes (barDb);
        }
    for (const auto& entry : fs::directory_iterator (refDir))
    {
        const auto name = entry.path().filename().string();
        if (entry.path().extension() == ".raw" && seen.count (name) == 0)
        {
            std::printf ("%-43s FAIL: missing from this tree\n", name.c_str());
            failed = true;
        }
    }
    std::printf ("\n%s\n", failed ? "Null check failed" : "Null check passed");
    return failed ? 1 : 0;
}

} // namespace

int main (int argc, char** argv)
{
    const std::string command = argc > 1 ? argv[1] : "";
    if (command == "render" && argc == 3)
        return render (argv[2]);
    if (command == "compare" && argc == 4)
        return compare (argv[2], argv[3]);
    std::fprintf (stderr, "usage: comp1_render render <dir> | compare <dir> <ref dir>\n");
    return 2;
}
