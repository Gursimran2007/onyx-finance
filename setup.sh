#!/bin/bash
set -e

echo "=== Finance App Setup (macOS) ==="

# 1. Check for Homebrew
if ! command -v brew &>/dev/null; then
    echo "[!] Homebrew not found. Installing..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
else
    echo "[1/5] Homebrew found, skipping install."
fi

# 2. Install system deps via brew
echo "[2/5] Installing cmake, curl, sqlite..."
brew install cmake curl sqlite git

# 3. Download Crow (header-only)
echo "[3/5] Downloading Crow..."
mkdir -p libs/crow
curl -sL "https://github.com/CrowCpp/Crow/releases/latest/download/crow_all.h" \
     -o libs/crow/crow.h

# 4. Download nlohmann/json (header-only)
echo "[4/5] Downloading nlohmann/json..."
mkdir -p libs/json/include/nlohmann
curl -sL "https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp" \
     -o libs/json/include/nlohmann/json.hpp

# 5. Clone SQLiteCpp
if [ ! -d "libs/SQLiteCpp" ]; then
    echo "[5/5] Cloning SQLiteCpp..."
    git clone --depth=1 https://github.com/SRombauts/SQLiteCpp.git libs/SQLiteCpp
else
    echo "[5/5] SQLiteCpp already cloned, skipping."
fi

# 6. Build
echo "[6/6] Building with CMake..."
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.logicalcpu)
cd ..

echo ""
echo "=== Build complete! ==="
echo ""
echo "Next steps:"
echo "  export ANTHROPIC_API_KEY=your_key_here"
echo "  ./build/finance_app"
echo ""
echo "Then open: http://localhost:8080"
