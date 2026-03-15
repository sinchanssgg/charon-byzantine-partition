#include "RippleLoader.h"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: generate_ripple_ned <ripple.csv> <output.ned> "
                  << "[maxNodes] [t] [c] [deltaMax]\n";
        return 1;
    }
    std::string csvPath = argv[1];
    std::string nedPath = argv[2];
    int maxNodes = (argc > 3) ? std::stoi(argv[3]) : 5000;
    int t        = (argc > 4) ? std::stoi(argv[4]) : 50;
    int c        = (argc > 5) ? std::stoi(argv[5]) : 5;
    int deltaMax = (argc > 6) ? std::stoi(argv[6]) : 8;

    auto topo = charon::RippleLoader::load(csvPath, maxNodes);
    charon::RippleLoader::writeNed(topo, nedPath, t, c, deltaMax);

    std::cout << "Done. Run OMNeT++ with: -n " << nedPath << "\n";
    return 0;
}
