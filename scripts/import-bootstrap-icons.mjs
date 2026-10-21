import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const destination = join(root, 'data/icons/bootstrap');
const groups = JSON.parse(readFileSync(join(destination, 'aliases.json'), 'utf8'));
const version = '1.13.1';
const aliases = new Map();
const canonicalGroups = [];

for (const [source, names] of Object.entries(groups)) {
  const canonicalNames = new Set(names.map(name => name.replace(/-symbolic$/, '')));
  for (const name of [...canonicalNames]) {
    if (name.startsWith('markdown-')) canonicalNames.add(`${name}-dark`);
  }
  for (const name of canonicalNames) {
    if (aliases.has(name)) throw new Error(`Duplicate alias: ${name}`);
    aliases.set(name, source);
  }
  canonicalGroups.push([source, ...canonicalNames]);
}

const generated = 'set(bootstrap_icon_groups\n' +
  canonicalGroups.map(group => `\t"${group.join('|')}"`).join('\n') + '\n)\n';

if (process.argv[2] !== '--check') {
  if (!process.argv[2]) throw new Error('Usage: node scripts/import-bootstrap-icons.mjs PACKAGE_DIRECTORY | --check');
  const upstream = resolve(process.argv[2]);
  const metadata = JSON.parse(readFileSync(join(upstream, 'package.json'), 'utf8'));
  if (metadata.name !== 'bootstrap-icons' || metadata.version !== version) {
    throw new Error(`Expected bootstrap-icons ${version}`);
  }
  mkdirSync(join(destination, 'svg'), { recursive: true });
  for (const source of Object.keys(groups)) {
    copyFileSync(join(upstream, 'icons', `${source}.svg`), join(destination, 'svg', `${source}.svg`));
  }
  copyFileSync(join(upstream, 'LICENSE'), join(destination, 'LICENSE'));
  writeFileSync(join(destination, 'aliases.cmake'), generated);
}

if (readFileSync(join(destination, 'aliases.cmake'), 'utf8') !== generated) {
  throw new Error('Regenerate aliases.cmake with the import command');
}
for (const source of Object.keys(groups)) {
  const filename = join(destination, 'svg', `${source}.svg`);
  if (!existsSync(filename)) throw new Error(`Missing Bootstrap SVG: ${source}`);
  execFileSync('xmllint', ['--noout', filename]);
}

const bundled = readFileSync(join(root, 'data/icons/CMakeLists.txt'), 'utf8');
const bundledNames = new Set([...bundled.matchAll(/hicolor_[a-z]+_[0-9a-z]+_([A-Za-z0-9_-]+)\.(?:png|svg)/g)]
  .map(match => match[1].replace(/-symbolic$/, '').replace(/-dark$/, '')));
const retainedArtwork = name => ['blueevolution', 'evolution'].includes(name) ||
  name.startsWith('im-') || name.startsWith('face-');
const missing = [...bundledNames].filter(name => !retainedArtwork(name) && !aliases.has(name));
if (missing.length) throw new Error(`Unmapped bundled icons: ${missing.join(', ')}`);
if (!readFileSync(join(destination, 'LICENSE'), 'utf8').includes('MIT License')) {
  throw new Error('Missing upstream MIT license');
}
console.log(`Validated Bootstrap Icons ${version}: ${Object.keys(groups).length} SVGs, ${aliases.size} application aliases`);