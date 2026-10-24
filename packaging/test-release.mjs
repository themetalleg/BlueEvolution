import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync, spawnSync } from 'node:child_process';
import test from 'node:test';
import YAML from 'yaml';
import { releaseMetadata, assertNewerRelease, writeInstallFiles } from './release.mjs';
import { mergeRepository, nextPublicationState, snapshotName } from './publish-flatpak.mjs';

const root = fileURLToPath(new URL('../', import.meta.url));

test('local preview uses a private installation without replacing signed channels', () => {
  const directory = mkdtempSync(join(tmpdir(), 'blueevolution-local-preview-'));
  try {
    const build = join(directory, 'build');
    mkdirSync(join(build, 'app'), { recursive: true });
    writeFileSync(join(build, 'app/metadata'), '[Application]\n');
    const manifest = { 'app-id': 'io.github.themetalleg.BlueEvolution.Beta', branch: 'beta', modules: [
      { name: 'evolution', 'config-opts': ['-DBLUEEVOLUTION_VERSION=0.2.3-dev.1'] }
    ] };
    writeFileSync(join(build, 'manifest.json'), JSON.stringify(manifest));
    const bundle = join(build, 'preview.flatpak');
    writeFileSync(bundle, 'synthetic bundle');
    const logfile = join(directory, 'commands.jsonl');
    const flatpak = join(directory, 'flatpak');
    writeFileSync(flatpak, '#!/usr/bin/env node\n' +
      'require("node:fs").appendFileSync(process.env.PREVIEW_TEST_LOG, JSON.stringify({args:process.argv.slice(2),installation:process.env.FLATPAK_USER_DIR,runtime:process.env.FLATPAK_SYSTEM_DIR,sessionRuntime:process.env.XDG_RUNTIME_DIR})+"\\n");\n' +
      'if(process.argv[2] === "info") { if(process.env.PREVIEW_TEST_COMMIT) process.stdout.write(process.env.PREVIEW_TEST_COMMIT+"\\n"); else process.exit(1); }\n',
      { mode: 0o755 });
    const result = spawnSync('bash', [resolve(root, 'scripts/vscode-blueevolution.sh'), 'preview', 'dark'], {
      env: { ...process.env, BUILD_ROOT: build, BUNDLE_PATH: bundle, BLUEEVOLUTION_PREVIEW_BUILD: 'false', PATH: `${directory}:${process.env.PATH}`,
        GDK_BACKEND: 'x11',
        DISPLAY: ':123',
        FLATPAK_USER_DIR: join(directory, 'signed-installation'),
        BLUEEVOLUTION_DEV_PROFILE: join(directory, 'profile'), PREVIEW_TEST_LOG: logfile }, encoding: 'utf8'
    });
    assert.equal(result.status, 0, result.stderr);
    const commands = readFileSync(logfile, 'utf8').trim().split('\n').map(line => JSON.parse(line));
    assert.ok(commands.length > 1);
    const installation = commands.find(command => command.args[0] === 'install');
    assert.ok(installation.args.includes('--or-update'));
    assert.ok(installation.args.includes(bundle));
    for (const command of commands) {
      assert.equal(command.installation, join(directory, 'profile/flatpak'));
      assert.equal(command.runtime, join(directory, 'signed-installation'));
    }
    for (const { args } of commands.filter(command => command.args[0] === 'run')) {
      assert.equal(args[0], 'run');
      assert.ok(args.includes('--user'));
      assert.ok(args.includes('--unshare=network'));
      assert.ok(args.includes('--no-documents-portal'));
      assert.ok(args.includes('--socket=x11'));
      assert.ok(args.includes('--nosocket=wayland'));
      assert.ok(args.includes('GDK_BACKEND=x11'));
      assert.ok(args.includes('DISPLAY=:123'));
      assert.ok(args.includes('--no-talk-name=org.freedesktop.secrets'));
      assert.ok(args.includes(`XDG_DATA_HOME=${join(directory, 'profile/data')}`));
      assert.ok(args.includes('BLUEEVOLUTION_DEMO_MODE=1'));
      assert.ok(!args.includes('install'));
    }
    assert.deepEqual(JSON.parse(readFileSync(join(build, 'manifest.json'))), manifest);
    assert.ok(commands.at(-1).args.includes('--offline'));
    assert.equal(commands.at(-1).sessionRuntime, join(directory, 'profile/runtime'));
  } finally { rmSync(directory, { recursive: true, force: true }); }
});

test('VS Code preview tasks offer only source-build light and dark modes', () => {
  const tasks = JSON.parse(readFileSync(resolve(root, '.vscode/tasks.json'), 'utf8')).tasks;
  assert.deepEqual(tasks.map(task => task.label), [
    'BlueEvolution: Preview Light', 'BlueEvolution: Preview Dark'
  ]);
  assert.deepEqual(tasks.map(task => task.args), [['preview', 'light'], ['preview', 'dark']]);
  for (const task of tasks) {
	assert.equal(task.command, '${workspaceFolder}/scripts/vscode-blueevolution.sh');
    assert.equal(task.options.env.BUILD_ROOT, '${workspaceFolder}/build/flatpak-local-preview');
    assert.equal(task.options.env.BLUEEVOLUTION_CHANNEL, 'beta');
    assert.equal(task.options.env.BLUEEVOLUTION_VERSION, '0.2.3-dev.1');
    assert.equal(task.options.env.FLATPAK_BUILD_JOBS, '2');
  }
});

test('preview builds try persistent Ninja incrementally before a clean fallback', () => {
  const preview = readFileSync(resolve(root, 'packaging/build-preview.sh'), 'utf8');
  const incremental = readFileSync(resolve(root, 'scripts/vscode-blueevolution-incremental.sh'), 'utf8');
  assert.ok(preview.includes('vscode-blueevolution-incremental.sh'));
  assert.ok(preview.indexOf('vscode-blueevolution-incremental.sh') < preview.indexOf('FLATPAK_BUILD_FORCE_CLEAN=true'));
  assert.ok(preview.includes('Preview build is current'));
  assert.ok(incremental.includes('/usr/bin/ninja -j'));
  assert.ok(incremental.includes('/usr/bin/ninja -j"${FLATPAK_BUILD_JOBS:-2}" -C /run/build/evolution install'));
  assert.ok(incremental.includes('OVERRIDE_APPLICATION_ID:BOOL=$app_id'));
  assert.ok(incremental.includes('Build configuration changed; incremental preview is not applicable'));
});

test('snapshot assets remain immutable across publications and retries', () => {
  assert.notEqual(snapshotName('100'), snapshotName('101'));
  assert.notEqual(snapshotName('100', '1'), snapshotName('100', '2'));
  assert.equal(snapshotName('100', '2'), 'BlueEvolution-updates-100-2.tar.gz');
  assert.throws(() => snapshotName('../unsafe'));
});

test('stable and prerelease versions have independent identities and refs', () => {
  const stable = releaseMetadata('0.2.0');
  const beta = releaseMetadata('0.3.0-beta.1');
  assert.equal(stable.channel, 'stable');
  assert.equal(beta.channel, 'beta');
  assert.notEqual(stable.app_id, beta.app_id);
  assert.notEqual(stable.ref, beta.ref);
  assert.equal(releaseMetadata('0.3.0-rc.1').channel, 'beta');
  assert.equal(beta.tag, 'blueevolution-v0.3.0-beta.1');
  for (const invalid of ['v1.0.0', '1.0', '../1.0.0', '1.0.0\nmalicious=true', '01.0.0']) {
    assert.throws(() => releaseMetadata(invalid));
  }
});

test('updates cannot replace a version or move a channel backwards', () => {
  assertNewerRelease('0.3.0-beta.10', '0.3.0-beta.9');
  assertNewerRelease('0.3.0', '0.2.9');
  assert.throws(() => assertNewerRelease('0.3.0-beta.9', '0.3.0-beta.10'));
  assert.throws(() => assertNewerRelease('0.3.0', '0.3.0'));
  assert.throws(() => assertNewerRelease('0.2.0', '0.3.0'));
});

test('AppStream transformation preserves one valid component and channel release', () => {
  const input = '<component type="desktop-application"><id>synthetic.desktop</id><name>Synthetic</name>' +
    '<releases><release version="3.62.1" type="stable"/></releases></component>';
  for (const [version, type] of [['0.2.0', 'stable'], ['0.3.0-beta.1', 'development']]) {
    const xml = execFileSync('xsltproc', ['--nonet', '--stringparam', 'version', version,
      '--stringparam', 'type', type, '--stringparam', 'date', '2026-10-24',
      resolve(root, 'packaging/release-metainfo.xsl'), '-'], { input, encoding: 'utf8' });
    const xpath = expression => execFileSync('xmllint', ['--xpath', expression, '-'], { input: xml, encoding: 'utf8' }).trim();
    assert.equal(xpath('count(/component)'), '1');
    assert.equal(xpath('count(/component/releases/release)'), '1');
    assert.equal(xpath('string(/component/id)'), 'synthetic.desktop');
    assert.equal(xpath('string(/component/releases/release/@version)'), version);
    assert.equal(xpath('string(/component/releases/release/@type)'), type);
    assert.equal(xpath('string(/component/releases/release/@date)'), '2026-10-24');
  }
});

test('publishing one channel preserves the other and rejects signing-key changes', () => {
  const stable = nextPublicationState(releaseMetadata('0.2.0'), null, 'synthetic-key');
  const both = nextPublicationState(releaseMetadata('0.3.0-beta.1'), stable, 'synthetic-key');
  assert.deepEqual(both.channels.stable, stable.channels.stable);
  assert.equal(both.channels.beta.version, '0.3.0-beta.1');
  const updated = nextPublicationState(releaseMetadata('0.2.1'), both, 'synthetic-key');
  assert.deepEqual(updated.channels.beta, both.channels.beta);
  assert.throws(() => nextPublicationState(releaseMetadata('0.2.2'), updated, 'different-key'));
  assert.throws(() => nextPublicationState(releaseMetadata('0.2.0'), updated, 'synthetic-key'));
  assert.throws(() => nextPublicationState({ ...releaseMetadata('0.3.0-beta.2'), channel: 'stable' }, updated, 'synthetic-key'));
});

test('generated manifests separate application, window and EDS service identities', () => {
  const directory = mkdtempSync(join(tmpdir(), 'blueevolution-channels-'));
  try {
    for (const version of ['0.2.0', '0.3.0-beta.1']) {
      const metadata = releaseMetadata(version);
      const buildRoot = join(directory, metadata.channel);
      const result = spawnSync('bash', [resolve(root, 'packaging/build-flatpak.sh'), '--prepare-only'], {
        env: { ...process.env, BUILD_ROOT: buildRoot, BLUEEVOLUTION_VERSION: version, BLUEEVOLUTION_CHANNEL: metadata.channel },
        encoding: 'utf8'
      });
      assert.equal(result.status, 0, result.stderr);
      const manifest = JSON.parse(readFileSync(join(buildRoot, 'manifest.json')));
      assert.equal(manifest['app-id'], metadata.app_id);
      assert.equal(manifest.branch, metadata.channel);
      const evolution = manifest.modules.find(module => module.name === 'evolution');
      const eds = manifest.modules.find(module => module.name === 'evolution-data-server');
      assert.ok(evolution['config-opts'].includes(`-DOVERRIDE_APPLICATION_ID=${metadata.app_id}`));
      assert.ok(evolution['config-opts'].includes(`-DWITH_WMCLASS_OVERRIDE=${metadata.app_id}`));
      assert.ok(evolution['config-opts'].includes(`-DBLUEEVOLUTION_APPLICATION_NAME=${metadata.app_name}`));
      assert.ok(eds['config-opts'].includes(`-DDBUS_SERVICES_PREFIX=${metadata.app_id}`));
      assert.ok(evolution['post-install'].some(command => command.includes(`${metadata.app_id}.metainfo.xml`)));
      const appdata = manifest.modules.find(module => module.name === 'news-to-appdata');
      assert.deepEqual(appdata['build-commands'], [`./update-appdata.sh ${metadata.channel === 'beta' ? 'development' : 'stable'}`]);
      const releaseEntry = appdata.sources[0].commands.find(command => command.startsWith('xsltproc ') && command.includes(version));
      assert.ok(releaseEntry);
      const entry = spawnSync('sh', ['-n'], { input: appdata.sources[0].commands.join('\n'), encoding: 'utf8' });
      assert.equal(entry.status, 0, entry.stderr);
      assert.ok(releaseEntry.includes(`--stringparam version ${version}`));
      assert.ok(releaseEntry.includes(`/app/share/metainfo/${metadata.app_id}.metainfo.xml`));
      assert.ok(appdata.sources.some(source => source.path?.endsWith('/packaging/release-metainfo.xsl')));
    }
    const result = spawnSync('bash', [resolve(root, 'packaging/build-flatpak.sh'), '--prepare-only'], {
      env: { ...process.env, BUILD_ROOT: directory, BLUEEVOLUTION_VERSION: '0.3.0-beta.1', BLUEEVOLUTION_CHANNEL: 'stable' }
    });
    assert.notEqual(result.status, 0);
  } finally { rmSync(directory, { recursive: true, force: true }); }
});

test('CI and publication workflow YAML and embedded shell scripts are valid', () => {
  for (const filename of ['ci.yml', 'release.yml', 'publish.yml']) {
    const workflow = YAML.parse(readFileSync(resolve(root, '.github/workflows', filename), 'utf8'));
    assert.ok(workflow.on);
    assert.ok(workflow.jobs);
    for (const job of Object.values(workflow.jobs)) {
      for (const step of job.steps ?? []) {
        if (!step.run) continue;
        if (step.name === 'Run account-free regression tests' || step.name === 'Verify bundle checksum and account-free regressions') {
          assert.ok(step.run.includes('dbus-run-session -- xvfb-run --auto-servernum flatpak run --user'));
        }
        const result = spawnSync('bash', ['-n'], { input: step.run, encoding: 'utf8' });
        assert.equal(result.status, 0, `${filename}: ${step.name}: ${result.stderr}`);
      }
    }
  }
  const publication = YAML.parse(readFileSync(resolve(root, '.github/workflows/publish.yml'), 'utf8'));
  const release = YAML.parse(readFileSync(resolve(root, '.github/workflows/release.yml'), 'utf8'));
  const releaseSteps = release.jobs['draft-release'].steps;
  assert.ok(releaseSteps.findIndex(step => step.name === 'Install release tooling') <
    releaseSteps.findIndex(step => step.name === 'Resolve release version'));
  assert.ok(releaseSteps.findIndex(step => step.name === 'Install build tools and GNOME runtime') <
    releaseSteps.findIndex(step => step.name === 'Verify release tooling'));
  assert.equal(publication.concurrency['cancel-in-progress'], false);
  const steps = publication.jobs.publish.steps;
  assert.equal(steps.find(step => step.name === 'Deploy updates').with.publication_sha,
    '${{ steps.prepare.outputs.publication_sha }}');
  assert.equal(steps.find(step => step.name === 'Deploy updates').uses, './packaging/pages-deploy');
  assert.ok(steps.findIndex(step => step.name === 'Deploy updates') <
    steps.findIndex(step => step.run?.includes('publish-flatpak.mjs record')));
});

test('Pages action wrapper sets a unique identity and retains its runtime context', () => {
  const directory = mkdtempSync(join(tmpdir(), 'blueevolution-pages-action-'));
  try {
    mkdirSync(join(directory, '.pages-deploy-action/dist'), { recursive: true });
    writeFileSync(join(directory, '.pages-deploy-action/dist/index.js'),
      'console.log(JSON.stringify({sha:process.env.GITHUB_SHA,hasRuntime:process.env.ACTIONS_RUNTIME_TOKEN === "synthetic-runtime-token"}));');
    const wrapper = resolve(root, 'packaging/pages-deploy/index.cjs');
    const env = { ...process.env, GITHUB_WORKSPACE: directory, GITHUB_SHA: 'b'.repeat(40),
      INPUT_PUBLICATION_SHA: 'a'.repeat(40), ACTIONS_RUNTIME_TOKEN: 'synthetic-runtime-token' };
    const result = spawnSync(process.execPath, [wrapper], { env, encoding: 'utf8' });
    assert.equal(result.status, 0, result.stderr);
    assert.deepEqual(JSON.parse(result.stdout), { sha: 'a'.repeat(40), hasRuntime: true });
    assert.notEqual(spawnSync(process.execPath, [wrapper], {
      env: { ...env, INPUT_PUBLICATION_SHA: 'invalid' }, encoding: 'utf8'
    }).status, 0);
  } finally { rmSync(directory, { recursive: true, force: true }); }
});

test('both install links use signed updates without cross-channel replacement', () => {
  const directory = mkdtempSync(join(tmpdir(), 'blueevolution-installers-'));
  const key = Buffer.from('synthetic public key');
  try {
    const channels = { stable: { version: '0.2.0' }, beta: { version: '0.3.0-beta.1' } };
    writeInstallFiles(directory, 'https://themetalleg.github.io/BlueEvolution/', key, channels);
    for (const [filename, channel] of [['BlueEvolution.flatpakref', 'stable'], ['BlueEvolution-Beta.flatpakref', 'beta']]) {
      const contents = readFileSync(join(directory, filename), 'utf8');
      assert.ok(contents.includes(`Name=${releaseMetadata(channels[channel].version).app_id}\n`));
      assert.ok(contents.includes(`Branch=${channel}\n`));
      assert.ok(contents.includes('Url=https://themetalleg.github.io/BlueEvolution/repo\n'));
      assert.ok(contents.includes(`GPGKey=${key.toString('base64')}\n`));
      assert.ok(!contents.includes('GPGVerify=false'));
    }
    assert.throws(() => writeInstallFiles(directory, 'http://example.invalid', key, channels));
    assert.throws(() => writeInstallFiles(directory, 'https://example.invalid', Buffer.alloc(0), channels));
    assert.throws(() => writeInstallFiles(directory, 'https://example.invalid', key, { stable: channels.beta }));
  } finally { rmSync(directory, { recursive: true, force: true }); }
});

test('signed stable and beta install together and both receive updates', {
  skip: process.env.BLUEEVOLUTION_TEST_FLATPAK !== '1', timeout: 120_000
}, () => {
  const directory = mkdtempSync(join(tmpdir(), 'blueevolution-signed-updates-'));
  const previousHome = process.env.GNUPGHOME;
  const home = join(directory, 'gnupg');
  mkdirSync(home, { mode: 0o700 });
  process.env.GNUPGHOME = home;
  const command = (name, args, options = {}) => execFileSync(name, args, { encoding: 'utf8', ...options }).trim();
  try {
    command('gpg', ['--batch', '--pinentry-mode', 'loopback', '--passphrase', '', '--quick-generate-key',
      'Synthetic release test <release-test@example.invalid>', 'ed25519', 'sign', '0']);
    const fingerprint = command('gpg', ['--batch', '--with-colons', '--list-secret-keys'])
      .split('\n').find(line => line.startsWith('fpr:')).split(':')[9];
    const key = join(directory, 'public.gpg');
    command('gpg', ['--batch', '--export', '--output', key, fingerprint]);
    const repository = join(directory, 'repository');
    const exportApp = (version, signed = true) => {
      const metadata = releaseMetadata(version);
      const app = join(directory, version);
      mkdirSync(join(app, 'files/bin'), { recursive: true });
      mkdirSync(join(app, 'files/share/applications'), { recursive: true });
      writeFileSync(join(app, 'metadata'), `[Application]\nname=${metadata.app_id}\nruntime=org.gnome.Platform/x86_64/49\ncommand=blueevolution\n`);
      writeFileSync(join(app, 'files/bin/blueevolution'), `#!/bin/sh\necho '${version}'\n`, { mode: 0o755 });
      writeFileSync(join(app, `files/share/applications/${metadata.app_id}.desktop`),
        `[Desktop Entry]\nType=Application\nName=${metadata.app_name}\nExec=blueevolution\n`);
      command('flatpak', ['build-finish', app]);
      const incoming = join(directory, `incoming-${version}`);
      command('flatpak', ['build-export', ...(signed ? [`--gpg-sign=${fingerprint}`] : []), incoming, app, metadata.channel]);
      return { metadata, incoming };
    };
    const publish = version => {
      const { metadata, incoming } = exportApp(version);
      mergeRepository(repository, incoming, metadata, key, fingerprint);
      return metadata;
    };
    const stable = publish('1.0.0');
    const beta = publish('1.1.0-beta.1');
    const env = { ...process.env, FLATPAK_USER_DIR: join(directory, 'installation'),
      XDG_DATA_HOME: join(directory, 'data'), XDG_CONFIG_HOME: join(directory, 'config'), XDG_CACHE_HOME: join(directory, 'cache') };
    const flatpak = args => command('flatpak', args, { env });
    flatpak(['remote-add', '--user', `--gpg-import=${key}`, 'blueevolution-test', `file://${repository}`]);
    for (const metadata of [stable, beta]) {
      flatpak(['install', '--user', '--assumeyes', '--no-deps', '--no-related', 'blueevolution-test', metadata.ref]);
    }
    const oldStable = flatpak(['info', '--user', '--show-commit', stable.app_id]);
    const oldBeta = flatpak(['info', '--user', '--show-commit', beta.app_id]);
    publish('1.0.1');
    publish('1.1.0-beta.2');
    flatpak(['update', '--user', '--assumeyes', '--no-deps', '--no-related']);
    assert.notEqual(flatpak(['info', '--user', '--show-commit', stable.app_id]), oldStable);
    assert.notEqual(flatpak(['info', '--user', '--show-commit', beta.app_id]), oldBeta);
    assert.equal(flatpak(['info', '--user', '--show-ref', stable.app_id]), stable.ref);
    assert.equal(flatpak(['info', '--user', '--show-ref', beta.app_id]), beta.ref);
    const refs = command('ostree', ['refs', `--repo=${repository}`]);
    assert.ok(refs.includes(`history/${stable.app_id}/1`));
    assert.ok(refs.includes(`history/${beta.app_id}/1`));
    const unsigned = exportApp('1.0.2', false);
    assert.throws(() => mergeRepository(join(directory, 'reject-unsigned'), unsigned.incoming, unsigned.metadata, key, fingerprint));
  } finally {
    command('gpgconf', ['--kill', 'gpg-agent']);
    if (previousHome === undefined) delete process.env.GNUPGHOME;
    else process.env.GNUPGHOME = previousHome;
    rmSync(directory, { recursive: true, force: true });
  }
});