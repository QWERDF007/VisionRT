const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const { createHash } = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

assert.equal(process.platform, 'win32', 'This probe requires Windows CDB and matching native PDBs');
const build = path.resolve(process.argv[2] || 'build');
const cdb = process.argv[3] || path.join(process.env['ProgramFiles(x86)'], 'Windows Kits/10/Debuggers/x64/cdb.exe');
const ctest = process.argv[4] || 'ctest';
const output = path.resolve(process.argv[5] || path.join(build, 'ui-notification-probe'));
fs.mkdirSync(output, { recursive: true });
const inventory = spawnSync(ctest, ['--test-dir', build, '-N', '--show-only=json-v1'],
    { encoding: 'utf8', windowsHide: true, timeout: 30000 });
assert.ifError(inventory.error);
assert.equal(inventory.status, 0, inventory.stderr);
const tests = JSON.parse(inventory.stdout).tests;
const captures = [];

function capture(name, debuggerCommands, arguments_ = []) {
    const test = tests.find(candidate => candidate.name === name);
    assert.ok(test, 'CTest target not registered: ' + name);
    const environment = { ...process.env };
    for (const entry of test.properties.find(property => property.name === 'ENVIRONMENT').value) {
        const assignment = /^([^=]+)=(.*)$/s.exec(entry);
        if (!assignment) continue;
        for (const key of Object.keys(environment)) {
            if (key.toLowerCase() === assignment[1].toLowerCase()) delete environment[key];
        }
        environment[assignment[1]] = assignment[2];
    }
    const label = name + (arguments_.length ? '-pixel-probe' : '');
    const commands = path.join(output, label + '.cdb');
    fs.writeFileSync(commands, [
        '.reload /f ' + path.basename(test.command[0]),
        'sxe -c ".echo TARGET_EXIT; .lastevent; q" epr',
        'sxe -c ".echo TARGET_FAULT; kb 30; q" 80000003',
        'sxe -c ".echo TARGET_FAULT; .exr -1; kb 30; q" av',
        ...debuggerCommands,
        'g\n'
    ].join('; '));
    const result = spawnSync(cdb,
        ['-G', '-lines', '-y', path.dirname(test.command[0]), '-cf', commands, ...test.command, ...arguments_],
        { env: environment, encoding: 'utf8', windowsHide: true, timeout: 90000, maxBuffer: 8 * 1024 * 1024 });
    const log = (result.stdout || '') + (result.stderr || '');
    const logPath = path.join(output, label + '.log');
    fs.writeFileSync(logPath, log);
    captures.push({ name: label, status: result.status, log: logPath,
        sha256: createHash('sha256').update(log).digest('hex') });
    assert.ifError(result.error);
    assert.equal(result.status, 0, 'CDB did not finish: ' + logPath);
    assert.doesNotMatch(log, /^TARGET_FAULT\s*$/m, 'Native fault: ' + logPath);
    return log;
}

const lifetime = capture('viewport_lifetime_contract', []);
assert.match(lifetime, /^VIEWPORT_LIFETIME_PASS destroyed=32 reparented=16 parent_owned=16\s*$/m);
assert.match(lifetime, /Exit process [^\r\n]+, code 0\r?\n/);
const notifications = capture('ui_notifications_contract', [
    'bu ui_notifications_contract!visionrt::RuntimeEngine::pollLatest ".echo D02_POLL; gc"'
]);
assert.match(notifications, /^NOTIFICATION_CONTRACT_PASS source_pages=12288 phases=2 final_pixel=37\s*$/m);
assert.match(notifications, /Exit process [^\r\n]+, code 0\r?\n/);
const phases = ['idle', 'running'].map(phase => {
    const beginning = notifications.search(new RegExp('^QUIET_BEGIN ' + phase + '\r?$', 'm'));
    const ending = notifications.indexOf('QUIET_END ' + phase, beginning);
    assert.ok(beginning >= 0 && ending > beginning, 'Missing quiet phase: ' + phase);
    const polls = (notifications.slice(beginning, ending).match(/^D02_POLL\s*$/gm) || []).length;
    assert.ok(polls >= 100, 'Fewer than 100 actual pollLatest entries: ' + phase + '=' + polls);
    assert.match(notifications.slice(ending).split(/\r?\n/)[0],
        /^QUIET_END (idle|running) runtime=0 nodes=0 results=0 parameters=0 pixels=0 frames=0$/);
    return { phase, polls, notifications: 0 };
});
const pixel = capture('ui_notifications_contract', [
    'sxe -c ".echo PIXEL_GUARD_CAUGHT; .exr -1; kb 15; q" av'
], ['--probe-pixel-read']);
assert.match(pixel, /^PIXEL_GUARD_PROBE\s*$/m);
assert.match(pixel, /^PIXEL_GUARD_CAUGHT\s*$/m);
assert.match(pixel, /ExceptionCode: c0000005/);
assert.match(pixel, /Attempt to read from address/);
assert.doesNotMatch(pixel, /^UNEXPECTED_PIXEL_READ /m);
const summary = { phases, pixelReadNegativeControl: 'caught',
    viewportDestructions: 32, nativeExit: 0, captures };
fs.writeFileSync(path.join(output, 'summary.json'), JSON.stringify(summary, null, 2) + '\n');
console.log(JSON.stringify(summary));
