'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {parsePacketHeader,FramePolicy}=require('../main/web/preview.js');
const original=fs.readFileSync(path.join(__dirname,'out/cplus/packet-header.bin'));
const array=b=>b.buffer.slice(b.byteOffset,b.byteOffset+b.byteLength);
test('actual production C frame header parses in the shipped JS',()=>{
  const value=parsePacketHeader(array(original),155);
  assert.deepEqual(value,{boot:'1234abcd',sequence:91,token:7,age:12,length:123});
  const p=new FramePolicy();assert(p.receive(value.boot,value.sequence));assert.equal(p.remaining(value.age,0,100),188);
});
test('invalid packet metadata cannot enter decoding',()=>{
  for(const [at,value] of [[0,0],[4,2],[5,1],[7,31],[28,0]]){
    const b=Buffer.from(original);b[at]=value;assert.throws(()=>parsePacketHeader(array(b),155));
  }
  assert.throws(()=>parsePacketHeader(array(original),154));
  const b=Buffer.from(original);b.writeUInt32BE(0,16);assert.throws(()=>parsePacketHeader(array(b),155));
});
