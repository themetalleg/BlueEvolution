import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync, spawnSync } from 'node:child_process';
import test from 'node:test';
import YAML from 'yaml';
import { releaseMetadata, assertNewerRelease, writeInstallFiles } from './release.mjs';
import { mergeRepository, nextPublicationState } from './publish-flatpak.mjs';

const root = fileURLToPath(new URL('../', import.meta.url));

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
  assert.equal(steps.find(step => step.name === 'Deploy updates').env.GITHUB_SHA,
    '${{ steps.prepare.outputs.publication_sha }}');
  assert.ok(steps.findIndex(step => step.uses?.startsWith('actions/deploy-pages@')) <
    steps.findIndex(step => step.run?.includes('publish-flatpak.mjs record')));
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