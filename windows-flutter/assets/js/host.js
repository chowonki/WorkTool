var self=globalThis, window=undefined, global=globalThis;
var console={log(){},info(){},warn(){},error(){},debug(){}};
// quickjs-ng에는 Intl이 없어요. 웹(ko-KR)과 같게 천 단위 쉼표를 넣어요 (소수는 셋째 자리까지).
Number.prototype.toLocaleString=function(){const v=Number(this);if(!isFinite(v))return String(v);
 const neg=v<0;let [i,f]=Math.abs(Math.round(v*1000)/1000).toString().split('.');
 if(i.includes('e'))return String(v);i=i.replace(/\B(?=(\d{3})+(?!\d))/g,',');return (neg?'-':'')+i+(f?'.'+f:'');};
globalThis.__timers=[];
globalThis.setTimeout=(f,ms,...a)=>{__timers.push(()=>f(...a));return __timers.length;};
globalThis.setImmediate=(f,...a)=>setTimeout(f,0,...a);
globalThis.clearTimeout=()=>{};globalThis.setInterval=()=>0;globalThis.clearInterval=()=>{};
globalThis.__drain=()=>{let n=0;while(__timers.length&&n<1000000){__timers.shift()();n++;}return n>0;};
globalThis.__utf8enc=s=>{s=String(s);const out=[];for(let i=0;i<s.length;i++){let c=s.charCodeAt(i);
 if(c>=0xd800&&c<0xdc00&&i+1<s.length){const d=s.charCodeAt(i+1);if(d>=0xdc00&&d<0xe000){c=0x10000+((c-0xd800)<<10)+(d-0xdc00);i++;}}
 if(c<0x80)out.push(c);else if(c<0x800)out.push(0xc0|c>>6,0x80|c&63);else if(c<0x10000)out.push(0xe0|c>>12,0x80|c>>6&63,0x80|c&63);
 else out.push(0xf0|c>>18,0x80|c>>12&63,0x80|c>>6&63,0x80|c&63);}return new Uint8Array(out);};
globalThis.__utf8dec=b=>{let s='',i=0;const n=b.length;const parts=[];while(i<n){let c=b[i++];
 if(c>=0xf0){c=(c&7)<<18|(b[i++]&63)<<12|(b[i++]&63)<<6|(b[i++]&63);}
 else if(c>=0xe0){c=(c&15)<<12|(b[i++]&63)<<6|(b[i++]&63);}
 else if(c>=0xc0){c=(c&31)<<6|(b[i++]&63);}
 if(c>=0x10000){c-=0x10000;parts.push(0xd800+(c>>10),0xdc00+(c&1023));}else parts.push(c);
 if(parts.length>8192){s+=String.fromCharCode.apply(null,parts);parts.length=0;}}
 return s+String.fromCharCode.apply(null,parts);};
globalThis.TextEncoder=class{encode(s=''){return __utf8enc(s)}};
globalThis.TextDecoder=class{constructor(l='utf-8'){this.encoding=l}decode(b){if(!b)return '';if(b instanceof ArrayBuffer)b=new Uint8Array(b);else if(!(b instanceof Uint8Array))b=new Uint8Array(b.buffer,b.byteOffset,b.byteLength);return __utf8dec(b)}};
globalThis.Blob=class Blob{constructor(parts=[],opts={}){const arrs=parts.map(p=>p instanceof Blob?p._b:p instanceof Uint8Array?p:p instanceof ArrayBuffer?new Uint8Array(p):ArrayBuffer.isView(p)?new Uint8Array(p.buffer,p.byteOffset,p.byteLength):new TextEncoder().encode(String(p)));const n=arrs.reduce((a,b)=>a+b.length,0);this._b=new Uint8Array(n);let o=0;for(const a of arrs){this._b.set(a,o);o+=a.length;}this.size=n;this.type=opts.type||'';}arrayBuffer(){return Promise.resolve(this._b.buffer.slice(this._b.byteOffset,this._b.byteOffset+this._b.byteLength))}};
globalThis.__bytesOf=v=>v instanceof Blob?v._b:v instanceof Uint8Array?v:v instanceof ArrayBuffer?new Uint8Array(v):ArrayBuffer.isView(v)?new Uint8Array(v.buffer,v.byteOffset,v.byteLength):new TextEncoder().encode(String(v));
const __B64='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
const __B64R=(()=>{const r=new Uint8Array(256);for(let i=0;i<64;i++)r[__B64.charCodeAt(i)]=i;return r;})();
globalThis.__b64dec=s=>{let n=s.length;while(n>0&&s[n-1]==='=')n--;const out=new Uint8Array((n*3)>>2);let o=0;
 for(let i=0;i<n;i+=4){const a=__B64R[s.charCodeAt(i)],b=__B64R[s.charCodeAt(i+1)],c=i+2<n?__B64R[s.charCodeAt(i+2)]:0,d=i+3<n?__B64R[s.charCodeAt(i+3)]:0;
 const v=a<<18|b<<12|c<<6|d;out[o++]=v>>16&255;if(i+2<n)out[o++]=v>>8&255;if(i+3<n)out[o++]=v&255;}return out;};
globalThis.__b64enc=b=>{const parts=[];let cur='';for(let i=0;i<b.length;i+=3){const v=b[i]<<16|(i+1<b.length?b[i+1]<<8:0)|(i+2<b.length?b[i+2]:0);
 cur+=__B64[v>>18&63]+__B64[v>>12&63]+(i+1<b.length?__B64[v>>6&63]:'=')+(i+2<b.length?__B64[v&63]:'=');
 if(cur.length>65536){parts.push(cur);cur='';}}parts.push(cur);return parts.join('');};
globalThis.atob=s=>{const b=__b64dec(String(s));let r='';for(let i=0;i<b.length;i++)r+=String.fromCharCode(b[i]);return r;};
globalThis.btoa=s=>{s=String(s);const b=new Uint8Array(s.length);for(let i=0;i<s.length;i++)b[i]=s.charCodeAt(i)&255;return __b64enc(b);};
