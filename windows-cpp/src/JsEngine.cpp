#include "pch.h"
#include "JsEngine.h"
#include "Util.h"
#include <bcrypt.h>
#include <regex>
#include "quickjs.h"

#pragma comment(lib, "bcrypt.lib")

namespace wt
{
    // quickjs-ng에는 Intl·타이머·TextEncoder·Blob이 없어서 규칙과 엑셀 라이브러리가 쓰는 만큼만 채워요.
    static char const Polyfill[] = R"JS(
var self=globalThis, window=undefined, global=globalThis;
var console={log(){},info(){},warn(){},error(){},debug(){}};
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
)JS";

    // __nativeSpin(hash, count, sha1?): H = SHA(LE32(i) + H) 를 count번 반복해요 (암호 엑셀의 키 만들기).
    // 세 번째 값이 true면 SHA-1, 아니면 SHA-512. 해시는 Windows CNG로 계산해요.
    static JSValue NativeSpin(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
    {
        size_t size = 0;
        auto input = argc > 0 ? JS_GetUint8Array(context, &size, argv[0]) : nullptr;
        int32_t count = 0;
        if (!input || size == 0 || size > 64 || argc < 2 || JS_ToInt32(context, &count, argv[1]) || count < 0 || count > 10000000)
            return JS_ThrowTypeError(context, "__nativeSpin: bad arguments");
        auto sha1 = argc > 2 && JS_ToBool(context, argv[2]) > 0;
        BCRYPT_ALG_HANDLE algorithm{};
        if (BCryptOpenAlgorithmProvider(&algorithm, sha1 ? BCRYPT_SHA1_ALGORITHM : BCRYPT_SHA512_ALGORITHM, nullptr, 0) < 0)
            return JS_ThrowTypeError(context, "__nativeSpin: hash unavailable");
        uint8_t block[4 + 64], hash[64];
        ULONG length = static_cast<ULONG>(size), hashLength = sha1 ? 20 : 64;
        memcpy(hash, input, size);
        auto failed = false;
        for (int32_t i = 0; i < count && !failed; i++)
        {
            block[0] = static_cast<uint8_t>(i); block[1] = static_cast<uint8_t>(i >> 8);
            block[2] = static_cast<uint8_t>(i >> 16); block[3] = static_cast<uint8_t>(i >> 24);
            memcpy(block + 4, hash, length);
            failed = BCryptHash(algorithm, nullptr, 0, block, 4 + length, hash, hashLength) < 0;
            length = hashLength;
        }
        BCryptCloseAlgorithmProvider(algorithm, 0);
        if (failed) return JS_ThrowTypeError(context, "__nativeSpin: hash failed");
        return JS_NewUint8ArrayCopy(context, hash, length);
    }

    static JSValue TakeBytes(JSContext* context, JSValueConst, int, JSValueConst*)
    {
        auto engine = static_cast<JsEngine*>(JS_GetContextOpaque(context));
        auto value = JS_NewUint8ArrayCopy(context, engine->staged.data(), engine->staged.size());
        engine->staged.clear();
        engine->staged.shrink_to_fit();
        return value;
    }

    static int Interrupt(JSContext*, void* opaque)
    {
        return std::chrono::steady_clock::now() > static_cast<JsEngine*>(opaque)->deadline;
    }

    JsEngine::JsEngine(size_t memoryLimit, int timeoutSeconds, size_t stackSize) : timeout(timeoutSeconds)
    {
        runtime = JS_NewRuntime();
        if (!runtime) throw AppError{ L"규칙 엔진을 만들지 못했어요." };
        JS_SetMemoryLimit(runtime, memoryLimit);
        JS_SetMaxStackSize(runtime, stackSize);
        JS_SetInterruptHandler(runtime, Interrupt, this);
        context = JS_NewContext(runtime);
        if (!context) { JS_FreeRuntime(runtime); throw AppError{ L"규칙 엔진을 만들지 못했어요." }; }
        JS_SetContextOpaque(context, this);
        auto global = JS_GetGlobalObject(context);
        JS_SetPropertyStr(context, global, "__nativeSpin", JS_NewCFunction(context, NativeSpin, "__nativeSpin", 3));
        JS_SetPropertyStr(context, global, "__takeBytes", JS_NewCFunction(context, TakeBytes, "__takeBytes", 0));
        JS_FreeValue(context, global);
        Eval(Polyfill, "polyfill");
    }

    JsEngine::~JsEngine()
    {
        JS_UpdateStackTop(runtime);
        JS_FreeContext(context);
        JS_FreeRuntime(runtime);
    }

    // 예외를 사용자에게 보여 줄 한 줄로: 첫 줄에서 "Error: " 같은 머리를 떼요.
    std::wstring JsEngine::ExceptionText()
    {
        auto exception = JS_GetException(context);
        std::string text = "알 수 없는 오류";
        if (auto message = JS_ToCString(context, exception)) { text = message; JS_FreeCString(context, message); }
        else { auto second = JS_GetException(context); JS_FreeValue(context, second); }
        JS_FreeValue(context, exception);
        auto wide = ToWide(text.substr(0, text.find('\n')));
        static std::wregex const head(LR"(^(Error|TypeError|RangeError|ReferenceError|SyntaxError|InternalError):\s*)");
        return Trim(std::regex_replace(wide, head, L""));
    }

    void JsEngine::Pending()
    {
        for (;;)
        {
            JSContext* current = nullptr;
            auto result = JS_ExecutePendingJob(runtime, &current);
            if (result == 0) return;
            if (result < 0) { if (current) { auto error = JS_GetException(current); JS_FreeValue(current, error); } return; }
        }
    }

    std::string JsEngine::Eval(std::string_view code, char const* name)
    {
        JS_UpdateStackTop(runtime);
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
        std::string source(code); // JS_Eval은 끝이 0인 버퍼를 기대해요.
        auto value = JS_Eval(context, source.c_str(), source.size(), name, JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(value)) throw AppError{ ExceptionText() };
        size_t length = 0;
        auto text = JS_ToCStringLen(context, &length, value);
        if (!text) { JS_FreeValue(context, value); throw AppError{ ExceptionText() }; }
        std::string result(text, length);
        JS_FreeCString(context, text);
        JS_FreeValue(context, value);
        Pending();
        return result;
    }

    std::string JsEngine::Json(std::string_view expression)
    {
        std::string code = "(()=>{const __jsonOut=(";
        code += expression;
        code += ");return __jsonOut===undefined?\"null\":JSON.stringify(__jsonOut)})()";
        return Eval(code);
    }

    void JsEngine::Wait(std::string_view expression)
    {
        std::string code = "globalThis.__done=false;globalThis.__fail=null;globalThis.__result=undefined;"
            "Promise.resolve().then(()=>(";
        code += expression;
        code += ")).then(v=>{__result=v;__done=true},e=>{__fail=String(e&&e.message||e);__done=true});";
        Eval(code);
        auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
        for (auto idle = 0;;)
        {
            Pending();
            auto state = Eval("__done?(__fail===null?\"ok\":\"fail\"):(__drain()?\"busy\":\"idle\")");
            if (state == "ok") return;
            if (state == "fail") throw AppError{ ToWide(Eval("__fail")) };
            if (state == "idle" && ++idle > 50) throw AppError{ L"작업이 끝나지 않았어요." };
            if (state == "busy") idle = 0;
            if (std::chrono::steady_clock::now() > limit) throw AppError{ L"작업 시간이 너무 오래 걸려요." };
        }
    }
}
