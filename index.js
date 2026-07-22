// Resolves a bundled prebuild for the current platform/arch when available
// (prebuilds/<platform>-<arch>/node.napi.node), otherwise falls back to a
// locally compiled build/Release/lzf.node.
module.exports = require('node-gyp-build')(__dirname);
