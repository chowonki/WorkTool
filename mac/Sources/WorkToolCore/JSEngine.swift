import Foundation
import JavaScriptCore

public struct EngineError: LocalizedError {
    public let message: String
    public init(_ message: String) { self.message = message }
    public var errorDescription: String? { message }
}

/// 웹과 같은 규칙 JavaScript를 실행하는 엔진이에요. 브라우저·DOM·네트워크·파일 접근은 주지 않아요.
/// JSContext는 스레드에 안전하지 않아서 모든 실행을 전용 직렬 큐에서 해요.
public final class JSEngine: @unchecked Sendable {
    private let queue = DispatchQueue(label: "worktool.rules", qos: .userInitiated)
    private var context: JSContext!
    private var timers: [JSValue] = []
    private var lastError: String?

    public init(scripts: [(name: String, source: String)]) throws {
        var failure: Error?
        queue.sync {
            do { try self.setUp(scripts) } catch { failure = error }
        }
        if let failure { throw failure }
    }

    /// 엔진 큐에서 작업을 실행해요.
    public func run<T>(_ body: @escaping (JSEngine) throws -> T) async throws -> T {
        try await withCheckedThrowingContinuation { cont in
            queue.async {
                do { cont.resume(returning: try body(self)) } catch { cont.resume(throwing: error) }
            }
        }
    }

    public func runSync<T>(_ body: (JSEngine) throws -> T) throws -> T {
        try queue.sync { try body(self) }
    }

    // MARK: - 큐 안에서만 부르는 함수

    private func setUp(_ scripts: [(name: String, source: String)]) throws {
        let ctx = JSContext()!
        context = ctx
        ctx.name = "WorkTool rules"
        ctx.exceptionHandler = { [weak self] _, exc in
            let msg = exc?.objectForKeyedSubscript("message")?.toString() ?? exc?.toString() ?? "알 수 없는 오류"
            self?.lastError = msg
        }
        let log: @convention(block) (String) -> Void = { msg in
            #if DEBUG
            print("[js]", msg)
            #endif
        }
        ctx.setObject(log, forKeyedSubscript: "__log" as NSString)
        let setTimeout: @convention(block) (JSValue, JSValue) -> Int = { [weak self] fn, _ in
            self?.timers.append(fn); return self?.timers.count ?? 0
        }
        ctx.setObject(setTimeout, forKeyedSubscript: "setTimeout" as NSString)
        let enc: @convention(block) (String) -> JSValue = { [weak self] s in self!.makeBytes(Data(s.utf8)) }
        let dec: @convention(block) (JSValue) -> String = { [weak self] v in String(decoding: self!.bytes(of: v), as: UTF8.self) }
        ctx.setObject(enc, forKeyedSubscript: "__enc" as NSString)
        ctx.setObject(dec, forKeyedSubscript: "__dec" as NSString)
        try eval(Self.polyfill, name: "polyfill")
        for s in scripts { try eval(s.source, name: s.name) }
    }

    private static let polyfill = #"""
    var self=globalThis, window=undefined;
    var console={log:(...a)=>__log(a.join(' ')),info:(...a)=>__log(a.join(' ')),warn:(...a)=>__log(a.join(' ')),error:(...a)=>__log(a.join(' '))};
    globalThis.setImmediate=(f,...a)=>setTimeout(()=>f(...a),0);
    globalThis.clearTimeout=()=>{};globalThis.setInterval=()=>0;globalThis.clearInterval=()=>{};
    globalThis.TextEncoder=class{encode(s=''){return __enc(String(s))}};
    globalThis.TextDecoder=class{constructor(l='utf-8'){this.encoding=l}decode(b){if(!b)return '';if(b instanceof ArrayBuffer)b=new Uint8Array(b);else if(!(b instanceof Uint8Array))b=new Uint8Array(b.buffer,b.byteOffset,b.byteLength);return __dec(b)}};
    globalThis.Blob=class Blob{constructor(parts=[],opts={}){const arrs=parts.map(p=>p instanceof Blob?p._b:p instanceof Uint8Array?p:p instanceof ArrayBuffer?new Uint8Array(p):ArrayBuffer.isView(p)?new Uint8Array(p.buffer,p.byteOffset,p.byteLength):new TextEncoder().encode(String(p)));const n=arrs.reduce((a,b)=>a+b.length,0);this._b=new Uint8Array(n);let o=0;for(const a of arrs){this._b.set(a,o);o+=a.length;}this.size=n;this.type=opts.type||'';}arrayBuffer(){return Promise.resolve(this._b.buffer.slice(0))}};
    globalThis.__bytesOf=v=>v instanceof Blob?v._b:v instanceof Uint8Array?v:v instanceof ArrayBuffer?new Uint8Array(v):ArrayBuffer.isView(v)?new Uint8Array(v.buffer,v.byteOffset,v.byteLength):new TextEncoder().encode(String(v));
    """#

    @discardableResult
    public func eval(_ source: String, name: String = "eval") throws -> JSValue {
        lastError = nil
        let v = context.evaluateScript(source, withSourceURL: URL(string: "worktool://\(name)"))
        drain()
        if let e = lastError { lastError = nil; throw EngineError(e) }
        return v ?? JSValue(undefinedIn: context)
    }

    /// 식의 결과를 JSON으로 받아 Swift 값(배열·사전·문자열·숫자)으로 돌려줘요.
    public func json(_ expression: String) throws -> Any {
        let v = try eval("JSON.stringify(\(expression))")
        guard v.isString, let s = v.toString(), let d = s.data(using: .utf8) else { return NSNull() }
        return try JSONSerialization.jsonObject(with: d, options: [.fragmentsAllowed])
    }

    /// 문자열 결과를 그대로 받아요(BOM 같은 글자도 지키려고 JSON을 거치지 않아요).
    public func string(_ expression: String) throws -> String {
        try eval("String(\(expression))").toString() ?? ""
    }

    /// 전역 변수에 Swift 값을 JSON으로 넣어요.
    public func set(_ name: String, _ value: Any) throws {
        let data = try JSONSerialization.data(withJSONObject: value, options: [.fragmentsAllowed])
        context.setObject(String(decoding: data, as: UTF8.self), forKeyedSubscript: "__json" as NSString)
        try eval("globalThis.\(name)=JSON.parse(__json);")
    }

    public func setBytes(_ name: String, _ data: Data) {
        context.setObject(makeBytes(data), forKeyedSubscript: name as NSString)
    }

    public func setString(_ name: String, _ value: String?) {
        context.setObject(value.map { $0 as NSString } ?? NSNull(), forKeyedSubscript: name as NSString)
    }

    /// Promise를 돌려주는 식을 끝까지 기다려요. 결과는 globalThis.__result에 남아요.
    public func await(_ expression: String) throws -> JSValue {
        try eval("globalThis.__done=false;globalThis.__fail=null;globalThis.__result=undefined;Promise.resolve().then(()=>(\(expression))).then(v=>{__result=v;__done=true},e=>{__fail=String(e&&e.message||e);__done=true});")
        var guardCount = 0
        while context.objectForKeyedSubscript("__done").toBool() == false {
            if timers.isEmpty { guardCount += 1; if guardCount > 3 { throw EngineError("작업이 끝나지 않았어요.") } }
            drain()
        }
        if let fail = context.objectForKeyedSubscript("__fail"), !fail.isNull, !fail.isUndefined {
            throw EngineError(fail.toString())
        }
        return context.objectForKeyedSubscript("__result")
    }

    public func bytes(_ expression: String) throws -> Data {
        bytes(of: try eval("__bytesOf(\(expression))"))
    }

    private func drain() {
        var n = 0
        while !timers.isEmpty && n < 1_000_000 {
            let f = timers.removeFirst()
            f.call(withArguments: [])
            n += 1
        }
    }

    private func makeBytes(_ d: Data) -> JSValue {
        let count = d.count
        let p = UnsafeMutableRawPointer.allocate(byteCount: max(count, 1), alignment: 8)
        d.copyBytes(to: p.assumingMemoryBound(to: UInt8.self), count: count)
        var exc: JSValueRef?
        let obj = JSObjectMakeTypedArrayWithBytesNoCopy(context.jsGlobalContextRef, kJSTypedArrayTypeUint8Array, p, count,
                                                        { bytes, _ in bytes?.deallocate() }, nil, &exc)
        return JSValue(jsValueRef: obj, in: context)
    }

    fileprivate func bytes(of v: JSValue) -> Data {
        var exc: JSValueRef?
        let ref = context.jsGlobalContextRef
        guard let obj = JSValueToObject(ref, v.jsValueRef, &exc) else { return Data() }
        let n = JSObjectGetTypedArrayByteLength(ref, obj, &exc)
        guard n > 0, let p = JSObjectGetTypedArrayBytesPtr(ref, obj, &exc) else { return Data() }
        let off = JSObjectGetTypedArrayByteOffset(ref, obj, &exc)
        return Data(bytes: p.advanced(by: off), count: n)
    }
}
