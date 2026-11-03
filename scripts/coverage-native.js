'use strict';

/* Line coverage of the native addon (src/), Windows only.
 *
 * 1. Copies binding.gyp + src/ into build-coverage/ and builds a Debug addon
 *    there with -Dlzf_fault_injection=1 (unoptimised, PDB, fault hooks). The
 *    regular build/ and prebuilds/ are never touched.
 * 2. Runs the test suite against that addon (NODE_LZF_BINDING) under Visual
 *    Studio's Microsoft.CodeCoverage.Console, which instruments the DLL
 *    dynamically from its PDB and writes Cobertura XML.
 * 3. Prints per-file line coverage for src/ and fails below 100 %.
 *
 * Needs Visual Studio 2022 17.3+ (any edition) with the C++ workload. */

const { spawnSync } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const workDir = path.join(root, 'build-coverage');
const report = path.join(workDir, 'native.cobertura.xml');

function fail(message) {
    console.error(`coverage-native: ${message}`);
    process.exit(1);
}

function run(command, args, options = {}) {
    const result = spawnSync(command, args, { stdio: 'inherit', ...options });
    if (result.error) fail(`${command}: ${result.error.message}`);
    if (result.status !== 0) fail(`${path.basename(command)} exited with ${result.status}`);
}

function findCoverageConsole() {
    const vswhere = path.join(
        process.env['ProgramFiles(x86)'] || 'C:\\Program Files (x86)',
        'Microsoft Visual Studio', 'Installer', 'vswhere.exe');
    if (!fs.existsSync(vswhere)) fail('vswhere.exe not found; install Visual Studio with the C++ workload');
    const found = spawnSync(vswhere, ['-all', '-products', '*', '-property', 'installationPath'], { encoding: 'utf8' });
    for (const install of found.stdout.split(/\r?\n/).filter(Boolean)) {
        const exe = path.join(install, 'Common7', 'IDE', 'Extensions', 'Microsoft',
            'CodeCoverage.Console', 'Microsoft.CodeCoverage.Console.exe');
        if (fs.existsSync(exe)) return exe;
    }
    return fail('Microsoft.CodeCoverage.Console.exe not found (Visual Studio 2022 17.3 or later)');
}

/* Cobertura → { "src/lzf.cc": Map(line → hits) } for files under build-coverage/src. */
function readCobertura(file) {
    const xml = fs.readFileSync(file, 'utf8');
    const srcDir = path.join(workDir, 'src').toLowerCase() + path.sep;
    const files = new Map();
    const classRe = /<class\b[^>]*\bfilename="([^"]+)"[^>]*>([\s\S]*?)<\/class>/g;
    for (let m; (m = classRe.exec(xml));) {
        const name = m[1];
        if (!name.toLowerCase().startsWith(srcDir)) continue;
        const key = path.join('src', name.slice(srcDir.length)).replace(/\\/g, '/');
        const lines = files.get(key) || new Map();
        for (const l of m[2].matchAll(/<line\b[^>]*\bnumber="(\d+)"[^>]*\bhits="(\d+)"/g)) {
            const n = Number(l[1]);
            lines.set(n, Math.max(lines.get(n) || 0, Number(l[2])));
        }
        files.set(key, lines);
    }
    return files;
}

if (process.platform !== 'win32') {
    fail('native coverage uses Visual Studio code coverage and runs on Windows only');
}

const coverageConsole = findCoverageConsole();

fs.rmSync(workDir, { recursive: true, force: true });
fs.mkdirSync(workDir, { recursive: true });
fs.copyFileSync(path.join(root, 'binding.gyp'), path.join(workDir, 'binding.gyp'));
fs.cpSync(path.join(root, 'src'), path.join(workDir, 'src'), { recursive: true });

const nodeGyp = require.resolve('node-gyp/bin/node-gyp.js');
run(process.execPath, [nodeGyp, 'rebuild', '--debug', '--lzf_fault_injection=1'], { cwd: workDir });

const addon = path.join(workDir, 'build', 'Debug', 'lzf.node');
run(coverageConsole, [
    'collect', '--nologo', '-f', 'cobertura', '-o', report, '-if', addon,
    process.execPath, '--test', path.join(root, 'test', 'test.js'),
], { cwd: root, env: { ...process.env, NODE_LZF_BINDING: addon } });

const files = readCobertura(report);
if (files.size === 0) fail(`no src/ lines in ${report}`);

let below = false;
console.log('\nnative line coverage (src/)');
for (const [file, lines] of [...files].sort()) {
    const missed = [...lines].filter(([, hits]) => hits === 0).map(([n]) => n).sort((a, b) => a - b);
    const pct = (100 * (lines.size - missed.length)) / lines.size;
    console.log(`  ${file.padEnd(20)} ${pct.toFixed(2).padStart(6)} %  ${lines.size - missed.length}/${lines.size}` +
        (missed.length ? `  uncovered: ${missed.join(', ')}` : ''));
    if (missed.length) below = true;
}
console.log(`report: ${report}`);
if (below) fail('native line coverage is below 100 %');
