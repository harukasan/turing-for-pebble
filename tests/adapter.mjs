import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { loadCore, WasmSimulation, effective } from '../lib/wasm-simulation.ts';
globalThis.fetch=async ()=>new Response(readFileSync('public/wasm/rd.wasm'));
const api=await loadCore();
const pixels={data:new Uint8ClampedArray(200*228*4)};
const params={feed:.029,kill:.057,da:1,db:.5,dt:1};
assert.equal(effective(params).feed,950/32768);
for(let i=0;i<30;i++){
 const s=new WasmSimulation(api,i%2,42);
 assert.equal(s.steps,0);s.step(params,10);assert.equal(s.steps,10);
 s.seedAt(0,0);s.render(pixels,'green',true);
 for(let p=0;p<pixels.data.length;p+=4){assert.equal(pixels.data[p+3],255);for(let c=0;c<3;c++)assert.equal(pixels.data[p+c]%85,0);}
 s.dispose();s.dispose();
}
console.log('TypeScript adapter: loading, 30 resets, stepping, seeding, RGB2 rows, disposal passed');
