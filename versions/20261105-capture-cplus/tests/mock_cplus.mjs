/* Local test-only RFC6455 subset. Firmware uses ESP-IDF's WebSocket server. */
import http from 'node:http';
import {createHash} from 'node:crypto';
import {readFileSync} from 'node:fs';
const root=new URL('../main/web/',import.meta.url);
const listen=server=>new Promise(resolve=>server.listen(0,'127.0.0.1',()=>resolve(server.address().port)));
function wire(op,payload){
  const n=payload.length,head=Buffer.alloc(n<126?2:4);head[0]=0x80|op;
  if(n<126)head[1]=n;else{head[1]=126;head.writeUInt16BE(n,2);}return Buffer.concat([head,payload]);
}
export async function createMock(){
  const template=readFileSync(new URL('./out/cplus/packet-header.bin',import.meta.url));
  const state={mode:'normal',boot:'1234abcd',sequence:0,statusFailure:false,clear:false,
    jpeg:readFileSync(new URL('./fixtures/color-full.jpg',import.meta.url)),credits:0,frames:0,status:0,maxOutstanding:0,pendingFrames:0,protocolViolations:0,shown:[],connections:0};
  const sockets=new Set();
  const stream=http.createServer((req,res)=>{res.writeHead(404);res.end();});
  stream.on('upgrade',(req,socket)=>{
    if(req.url!='/stream'||!req.headers['sec-websocket-key']){socket.destroy();return;}
    const accept=createHash('sha1').update(req.headers['sec-websocket-key']+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: '+accept+'\r\n\r\n');
    state.connections++;sockets.add(socket);let buffered=Buffer.alloc(0),flight=0,timer,pending=false;
    socket.on('error',()=>{});socket.on('close',()=>{clearTimeout(timer);if(pending){pending=false;state.pendingFrames--;}sockets.delete(socket);});
    socket.on('data',chunk=>{
      buffered=Buffer.concat([buffered,chunk]);
      while(buffered.length>=2){
        const op=buffered[0]&15,masked=!!(buffered[1]&128);let n=buffered[1]&127,at=2;
        if(n===126){if(buffered.length<4)return;n=buffered.readUInt16BE(2);at=4;}
        if(n===127||n>1024||!masked){socket.destroy();return;}
        if(buffered.length<at+4+n)return;
        const mask=buffered.subarray(at,at+4),body=Buffer.from(buffered.subarray(at+4,at+4+n));buffered=buffered.subarray(at+4+n);
        for(let i=0;i<body.length;i++)body[i]^=mask[i%4];
        if(op===8){socket.end(wire(8,body));return;}
        if(op===9){socket.write(wire(10,body));continue;}
        if(op!==1){socket.destroy();return;}
        const match=/^N (\d+) (\d+) ([01])$/.exec(body.toString());if(!match){socket.destroy();return;}
        const token=Number(match[1]),ack=Number(match[2]);
        if(pending||(flight&&ack!==flight)||(!flight&&ack)){state.protocolViolations++;socket.destroy();return;}
        if(flight)state.shown.push(Number(match[3]));flight=token;state.credits++;
        pending=true;state.pendingFrames++;state.maxOutstanding=Math.max(state.maxOutstanding,state.pendingFrames);
        if(state.mode==='no_frames')continue;
        timer=setTimeout(()=>{
          if(socket.destroyed)return;
          const head=Buffer.from(template);head.writeUInt32BE(parseInt(state.boot,16),8);
          head.writeUInt32BE(++state.sequence>>>0,12);head.writeUInt32BE(token,16);
          head.writeUInt32BE(state.mode==='late'?350:1,20);head.writeUInt32BE(state.jpeg.length,24);
          pending=false;state.pendingFrames--;socket.write(wire(2,Buffer.concat([head,state.jpeg])));state.frames++;
        },25);
      }
    });
  });
  const streamPort=await listen(stream);
  const server=http.createServer((req,res)=>{
    if(req.url==='/status'){
      state.status++;if(state.statusFailure){res.writeHead(503);res.end();return;}
      const channels=['red','green','blue'].map((color,i)=>({color,state:state.clear?0:[2,1,3][i],multiplier:state.clear?0:[4,2,2][i],on:state.clear?false:i<2}));
      res.setHeader('Content-Type','application/json');res.setHeader('Cache-Control','no-store');
      res.end(JSON.stringify({version:'LOCAL-MOCK-NOT-HARDWARE',boot_id:state.boot,stream_port:streamPort,
        capture_format:'MJPEG',capture_fps_config:60,stream_target_fps:30,received:state.frames,encoded:state.frames,sent:state.frames,
        dropped:0,encode_errors:0,send_errors:0,last_decode_ms:2,last_encode_ms:0,last_send_ms:1,last_ack_ms:5,
        mjpeg_prepare_errors:0,mjpeg_decode_errors:0,net_attempts:1,net_exhausted:false,
        lights:{valid:true,age_ms:1,epoch:9,enabled:true,batch_active:!state.clear,selected:state.clear?-1:0,four_color:0,channels}}));return;
    }
    if(req.url==='/preview.js'){res.setHeader('Content-Type','text/javascript;charset=utf-8');res.end(readFileSync(new URL('preview.js',root)));return;}
    if(req.url==='/'){res.setHeader('Content-Type','text/html;charset=utf-8');res.end(readFileSync(new URL('index.html',root)));return;}
    res.writeHead(404);res.end();
  });
  const port=await listen(server);
  return{state,url:`http://127.0.0.1:${port}/`,cut(){for(const socket of sockets)socket.destroy();},
    async close(){for(const socket of sockets)socket.destroy();await Promise.all([new Promise(r=>server.close(r)),new Promise(r=>stream.close(r))]);}};
}
