import test from 'node:test';
import assert from 'node:assert/strict';
import {deliveryQuery} from '../src/worker.mjs';
const id='12345678-1234-4234-8234-123456789abc';
const parse=q=>deliveryQuery(new URL('https://fixture.invalid/v1/jobs'+q));
test('delivery query preserves legacy semantics and supports only current payload IDs',()=>{assert.deepEqual(parse(''),{ids:null,limit:200});assert.deepEqual(parse('?limit=1'),{ids:null,limit:1});assert.deepEqual(parse('?ids='+id),{ids:[id],limit:200});});
test('delivery query rejects duplicate, unbounded and malformed parameters',()=>{for(const q of ['?limit=0','?limit=201','?limit=1e2','?limit=-1','?limit=1&limit=2','?ids=','?ids=bad','?ids='+id+','+id,'?ids='+id+'&ids='+id,'?ids='+Array.from({length:33},(_,i)=>id.replace(/....$/,String(i).padStart(4,'0'))).join(',')])assert.throws(()=>parse(q));});

test('native exclusions are bounded and malformed or duplicate IDs are rejected',async()=>{const {deviceDeliveryExclusions}=await import('../src/worker.mjs');const parse=q=>deviceDeliveryExclusions(new URL('https://fixture.invalid/v1/device/jobs'+q));assert.deepEqual(parse(''),[]);assert.deepEqual(parse('?exclude='+id),[id]);for(const q of ['?exclude=','?exclude=invalid','?exclude='+id+','+id,'?exclude='+id+'&exclude='+id,'?exclude='+Array.from({length:97},(_,i)=>id.replace(/....$/,String(i).padStart(4,'0'))).join(',')])assert.throws(()=>parse(q));});
