import { readFileSync, writeFileSync } from 'fs';
import { execSync } from 'child_process';

const state = JSON.parse(readFileSync('state.json', 'utf8'));
console.log('Current state:', state);

state.latest_tag = 'blueevolution-v0.2.13';
state.channels.stable = {
  version: '0.2.13',
  tag: 'blueevolution-v0.2.13'
};

writeFileSync('state.json', JSON.stringify(state, null, 2));
console.log('\nUpdated state:', state);

// Commit to flatpak-state branch
execSync('git config user.email "robot@github.com"');
execSync('git config user.name "GitHub Robot"');
execSync('git add state.json');
execSync('git commit -m "Update state to 0.2.13"');
execSync('git push origin HEAD:flatpak-state');

console.log('\nPushed updated state to flatpak-state branch');
