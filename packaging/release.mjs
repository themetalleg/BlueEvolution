import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import semver from 'semver';

export function releaseMetadata(version) {
  assert.equal(semver.valid(version), version, 'Use a complete semantic version without a v prefix');
  const channel = semver.prerelease(version) ? 'beta' : 'stable';
  const app_id = `io.github.themetalleg.BlueEvolution${channel === 'beta' ? '.Beta' : ''}`;
  return {
    version, channel, app_id,
    app_name: channel === 'beta' ? 'BlueEvolution Beta' : 'BlueEvolution',
    ref: `app/${app_id}/x86_64/${channel}`,
    tag: `blueevolution-v${version}`,
    prerelease: channel === 'beta'
  };
}

export function assertNewerRelease(version, previous) {
  releaseMetadata(version);
  if (previous) {
    releaseMetadata(previous);
    assert.ok(semver.gt(version, previous), `${version} must be newer than ${previous}`);
  }
}

export function writeInstallFiles(directory, baseUrl, publicKey, channels) {
  const url = new URL(baseUrl);
  assert.equal(url.protocol, 'https:', 'Public updates require HTTPS');
  assert.ok(!url.search && !url.hash && !url.username && !url.password);
  assert.ok(Buffer.isBuffer(publicKey) && publicKey.length > 0, 'A public signing key is required');
  const root = url.href.replace(/\/$/, '');
  const key = publicKey.toString('base64');
  mkdirSync(directory, { recursive: true });
  const links = [];
  for (const channel of ['stable', 'beta']) {
    if (!channels[channel]) continue;
    const metadata = releaseMetadata(channels[channel].version);
    assert.equal(metadata.channel, channel);
    const filename = channel === 'stable' ? 'BlueEvolution.flatpakref' : 'BlueEvolution-Beta.flatpakref';
    writeFileSync(resolve(directory, filename), [
      '[Flatpak Ref]', 'Version=1', `Name=${metadata.app_id}`, `Branch=${channel}`,
      `Title=${metadata.app_name}`, 'IsRuntime=false', `Url=${root}/repo`,
      'SuggestRemoteName=blueevolution', `GPGKey=${key}`,
      'RuntimeRepo=https://dl.flathub.org/repo/flathub.flatpakrepo', ''
    ].join('\n'));
    links.push(`<li><a href="${filename}">${metadata.app_name}</a> (${metadata.version})</li>`);
  }
  assert.ok(links.length, 'At least one published channel is required');
  writeFileSync(resolve(directory, 'BlueEvolution.flatpakrepo'), [
    '[Flatpak Repo]', 'Version=1', 'Title=BlueEvolution', `Url=${root}/repo`,
    `DefaultBranch=${channels.stable ? 'stable' : 'beta'}`, `GPGKey=${key}`, ''
  ].join('\n'));
  writeFileSync(resolve(directory, 'index.html'),
    '<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width">' +
    '<title>Install BlueEvolution</title><body><h1>BlueEvolution</h1><ul>' + links.join('') +
    '</ul><p>Stable and beta install separately. Updates are delivered through Flatpak.</p></body></html>\n');
  writeFileSync(resolve(directory, '.nojekyll'), '');
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const [command, version, previous] = process.argv.slice(2);
  if (command === 'metadata') console.log(JSON.stringify(releaseMetadata(version)));
  else if (command === 'check-newer') assertNewerRelease(version, previous);
  else throw new Error('Expected metadata VERSION or check-newer VERSION [PREVIOUS]');
}