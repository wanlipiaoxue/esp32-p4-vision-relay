import {createServer} from 'node:http';
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
export async function createMock() {
  const result = JSON.parse(readFileSync(resolve(root,'tests/out/preview/results.json'),'utf8'));
  const template = JSON.parse(result.logs[1].stdout.match(/^STATUS_LIVE (.+)$/m)[1]);
  const state = {mode:'normal',statusFailure:false,clear:false,boot:'1234abcd',bootAt:Date.now(),jpeg:null,
    frames:0,status:0,frameActive:0,statusActive:0,maxFrameActive:0,maxStatusActive:0,queries:[]};
  const server = createServer((req,res) => {
    const url = new URL(req.url,'http://localhost');
    res.setHeader('Cache-Control','no-store');
    if(url.pathname==='/'){res.setHeader('Content-Type','text/html; charset=utf-8');res.end(readFileSync(resolve(root,'main/web/index.html')));return;}
    if(url.pathname==='/preview.js'){res.setHeader('Content-Type','text/javascript; charset=utf-8');res.end(readFileSync(resolve(root,'main/web/preview.js')));return;}
    if(url.pathname==='/favicon.ico'){res.writeHead(204);res.end();return;}
    if(url.pathname==='/status'){
      state.status++;state.statusActive++;state.maxStatusActive=Math.max(state.maxStatusActive,state.statusActive);
      res.once('close',()=>state.statusActive--);
      if(state.statusFailure){res.writeHead(503);res.end('{}');return;}
      const data=structuredClone(template);data.boot_id=state.boot;
      data.received=Math.floor((Date.now()-state.bootAt)/100);data.encoded=data.received;data.sent=state.frames;
      data.lights.age_ms=2;
      if(state.clear){data.lights.epoch++;data.lights.batch_active=false;data.lights.selected=-1;
        data.lights.channels.forEach(c=>Object.assign(c,{state:0,multiplier:0,on:false}));}
      res.setHeader('Content-Type','application/json');res.end(JSON.stringify(data));return;
    }
    if(url.pathname==='/frame.jpg'){
      state.frames++;state.frameActive++;state.maxFrameActive=Math.max(state.maxFrameActive,state.frameActive);
      res.once('close',()=>state.frameActive--);state.queries.push(url.searchParams.get('after'));
      if(!state.jpeg){res.writeHead(503);res.end();return;}
      const seq=Math.floor((Date.now()-state.bootAt)/100)+1;
      if(state.mode==='no_frames'||(url.searchParams.get('after')===String(seq)&&url.searchParams.get('boot')===state.boot)){
        res.writeHead(204);res.end();return;
      }
      res.setHeader('Content-Type','image/jpeg');res.setHeader('X-Boot-Id',state.boot);
      res.setHeader('X-Frame-Seq',String(seq));res.setHeader('X-Frame-Age-Ms','10');
      res.setHeader('Content-Length',state.jpeg.length);res.flushHeaders();
      const finish=()=>{if(!res.destroyed)res.end(state.jpeg);};
      if(state.mode==='late')setTimeout(finish,360);else finish();
      return;
    }
    res.writeHead(404);res.end('not found');
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  return {server,state,url:`http://127.0.0.1:${server.address().port}/`};
}
