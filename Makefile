# ChromaOnion — After Effects effect plugin (macOS)
#
#   make            build a universal (arm64 + x86_64) .plugin into build/
#   make install    copy the built plugin into the AE common plug-ins folder
#   make clean      remove build artifacts
#
# Requires Xcode command line tools (clang++, Rez, ResMerger) and the After
# Effects SDK. By default the SDK is expected at ./AE_SDK (set SDK=... to override).

SDK        ?= AE_SDK
SDK_EX      = $(SDK)/Examples

NAME        = ChromaOnion
SRC         = src
MACDIR      = mac
BUILD       = build
BUNDLE      = $(BUILD)/$(NAME).plugin
CONTENTS    = $(BUNDLE)/Contents

INCLUDES    = -I$(SDK_EX)/Headers -I$(SDK_EX)/Util -I$(SDK_EX)/Headers/SP -I$(SDK_EX)/Resources
REZ_INC     = -i $(SDK_EX)/Headers -i $(SDK_EX)/Util -i $(SDK_EX)/Headers/SP -i $(SDK_EX)/Resources
ARCHS       = -arch arm64 -arch x86_64
MACOS_MIN   = -mmacosx-version-min=11.0
SDKROOT    := $(shell xcrun --show-sdk-path)

CXX         = clang++
CXXFLAGS    = -std=c++17 -fvisibility=hidden -Os $(ARCHS) $(MACOS_MIN) \
              -isysroot $(SDKROOT) $(INCLUDES) -Wno-deprecated-declarations
LDFLAGS     = -bundle $(ARCHS) $(MACOS_MIN) -isysroot $(SDKROOT) -framework Cocoa

REZ         = Rez
RESMERGER   = ResMerger

# Install destination. Override with e.g.
#   make install INSTALL_DIR="/Applications/Adobe After Effects 2025/Plug-ins"
INSTALL_DIR ?= /Applications/Adobe After Effects 2026/Plug-ins

.PHONY: all clean install

all: $(BUNDLE)

$(BUNDLE): $(SRC)/$(NAME).cpp $(SRC)/$(NAME).h $(SRC)/$(NAME)PiPL.r $(MACDIR)/$(NAME).plugin-Info.plist
	@echo "==> Creating bundle layout"
	@rm -rf "$(BUNDLE)"
	@mkdir -p "$(CONTENTS)/MacOS" "$(CONTENTS)/Resources"
	@echo "==> Compiling (universal)"
	$(CXX) $(CXXFLAGS) $(LDFLAGS) "$(SRC)/$(NAME).cpp" -o "$(CONTENTS)/MacOS/$(NAME)"
	@echo "==> Compiling PiPL resource (Rez)"
	$(REZ) -o "$(BUILD)/$(NAME)PiPL.rsrc" -d SystemSevenOrLater=1 -useDF -script Roman \
		-define __MACH__ $(REZ_INC) -isysroot $(SDKROOT) "$(SRC)/$(NAME)PiPL.r"
	$(RESMERGER) "$(BUILD)/$(NAME)PiPL.rsrc" -dstIs DF -o "$(CONTENTS)/Resources/$(NAME).rsrc"
	@echo "==> Writing Info.plist / PkgInfo"
	@cp "$(MACDIR)/$(NAME).plugin-Info.plist" "$(CONTENTS)/Info.plist"
	@printf 'eFKTFXTC' > "$(CONTENTS)/PkgInfo"
	@echo "==> Ad-hoc code signing"
	@codesign --force --sign - --timestamp=none "$(BUNDLE)"
	@echo "==> Built $(BUNDLE)"

install: $(BUNDLE)
	@mkdir -p "$(INSTALL_DIR)"
	@rm -rf "$(INSTALL_DIR)/$(NAME).plugin"
	@cp -R "$(BUNDLE)" "$(INSTALL_DIR)/"
	@echo "==> Installed to $(INSTALL_DIR)"

clean:
	@rm -rf "$(BUILD)"
	@echo "==> Cleaned"
