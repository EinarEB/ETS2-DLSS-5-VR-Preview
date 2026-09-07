// GPU check for the stereo depth matcher: the blocking exact proof, the
// one-frame-late identity assignment, its correction when the eyes swap, its
// fall-back when a resource changes or a proof fails, and the bounded hold.
// Needs a D3D11 hardware device, so test.cmd does not run it; use test-gpu.cmd.
#include "../src/depth/depth_match.hpp"
#include "../src/feeder/feed_context_protection.h"
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
using depth_match::Check;
using Microsoft::WRL::ComPtr;
static unsigned checks = 0;
static void require(bool v, const char* s)
{
    ++checks;
    if (!v)
        throw std::runtime_error(s);
}
int main()
{
    try
    {
        constexpr unsigned W = 37, H = 19;
        ComPtr<ID3D11Device> d;
        ComPtr<ID3D11DeviceContext> c;
        Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d, nullptr, &c), "device");
        auto texture = [&](unsigned width, DXGI_FORMAT format, const void* pixels) {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = width; td.Height = H; td.MipLevels = td.ArraySize = td.SampleDesc.Count = 1;
            td.Format = format; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA initial{pixels, width * 4, 0};
            ComPtr<ID3D11Texture2D> t;
            Check(d->CreateTexture2D(&td, pixels ? &initial : nullptr, &t), "test texture");
            return t;
        };
        // Every frame waits for the GPU, so the non-blocking readback of the previous
        // proof is always complete and the sequence below is deterministic.
        auto finish = [&]() {
            D3D11_QUERY_DESC q{D3D11_QUERY_EVENT, 0};
            ComPtr<ID3D11Query> done;
            Check(d->CreateQuery(&q, &done), "event query");
            c->End(done.Get()); c->Flush();
            const ULONGLONG deadline = GetTickCount64() + 10000;
            HRESULT hr;
            while ((hr = c->GetData(done.Get(), nullptr, 0, 0)) == S_FALSE)
            {
                require(GetTickCount64() < deadline, "GPU completion wait");
                Sleep(1);
            }
            Check(hr, "GPU completion");
        };
        std::vector<uint32_t> pixels[2];
        std::vector<float> z[2];
        ComPtr<ID3D11Texture2D> colors[2], depths[2];
        for (unsigned eye = 0; eye < 2; ++eye)
        {
            pixels[eye].resize(W * H); z[eye].resize(W * H);
            for (unsigned i = 0; i < W * H; ++i)
            {
                pixels[eye][i] = 0x80000000u | ((i * 747796405u + eye * 157u) & 0xffffffu);
                z[eye][i] = .15f + .5f * eye + .0001f * i;
            }
            colors[eye] = texture(W, DXGI_FORMAT_R8G8B8A8_UNORM, pixels[eye].data());
            depths[eye] = texture(W, DXGI_FORMAT_R32_FLOAT, z[eye].data());
        }
        std::vector<uint32_t> combinedPixels(W * 2 * H);
        auto assemble = [&](bool reverse) {
            for (unsigned y = 0; y < H; ++y)
                for (unsigned eye = 0; eye < 2; ++eye)
                    for (unsigned x = 0; x < W; ++x)
                        combinedPixels[y * W * 2 + eye * W + x] = pixels[reverse ? 1 - eye : eye][y * W + x];
        };
        assemble(false);
        auto combined = texture(W * 2, DXGI_FORMAT_R8G8B8A8_UNORM, combinedPixels.data());
        auto upload = [&]() { c->UpdateSubresource(combined.Get(), 0, nullptr, combinedPixels.data(), W * 2 * 4, 0); };
        auto shiftDepth = [&]() {
            for (unsigned eye = 0; eye < 2; ++eye)
            {
                for (auto& v : z[eye]) v += .07f;
                c->UpdateSubresource(depths[eye].Get(), 0, nullptr, z[eye].data(), W * 4, 0);
            }
        };
        ets2_d3d11::ProtectionRegistry protection;
        require(protection.Acquire(c.Get()), "device attachment");
        depth_match::Matcher matcher(d.Get(), c.Get(), W, H, DXGI_FORMAT_R32_FLOAT);
        auto capture = [&](uint64_t epoch, ID3D11Texture2D* first = nullptr) {
            matcher.Reset();
            for (unsigned eye = 0; eye < 2; ++eye)
                require(matcher.Capture(eye == 0 && first ? first : colors[eye].Get(), 0, depths[eye].Get(), 0, epoch, eye + 1), "capture");
        };
        auto output = [&]() {
            std::vector<float> out(W * 2 * H);
            require(matcher.Output() != nullptr, "assembled output present");
            D3D11_TEXTURE2D_DESC td{};
            matcher.Output()->GetDesc(&td);
            td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging;
            Check(d->CreateTexture2D(&td, nullptr, &staging), "readback");
            c->CopyResource(staging.Get(), matcher.Output());
            D3D11_MAPPED_SUBRESOURCE m{};
            Check(c->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m), "readback map");
            for (unsigned y = 0; y < H; ++y)
                memcpy(&out[y * W * 2], static_cast<const char*>(m.pData) + y * m.RowPitch, W * 2 * sizeof(float));
            c->Unmap(staging.Get(), 0);
            return out;
        };
        auto expect = [&](int leftSource, int rightSource, const char* what) {
            auto out = output();
            for (unsigned y = 0; y < H; ++y)
                for (unsigned eye = 0; eye < 2; ++eye)
                    for (unsigned x = 0; x < W; ++x)
                        require(out[y * W * 2 + eye * W + x] == z[eye == 0 ? leftSource : rightSource][y * W + x], what);
        };

        // 1. The blocking exact proof, as before the branch.
        capture(1);
        auto r = matcher.Match(combined.Get(), 1, false); finish();
        require(r.accepted && r.synchronous && r.proofAge == 0 && r.eye[0] == 0 && r.eye[1] == 1, "blocking proof accepts the reference frame");
        expect(0, 1, "blocking proof assembles the right eyes");
        // 2. Asynchronous mode reuses the verified assignment when the resources repeat.
        shiftDepth(); capture(2);
        r = matcher.Match(combined.Get(), 2, true); finish();
        require(r.accepted && !r.synchronous && r.proofAge == 1 && r.eye[0] == 0 && r.eye[1] == 1, "second frame assigned by identity from a proof one frame old");
        expect(0, 1, "identity assignment assembles the current depth");
        // 3. The eyes swap inside the same resources: one frame carries the stale
        //    assignment, the next frame's retired proof corrects it without blocking.
        assemble(true); upload(); shiftDepth(); capture(3);
        r = matcher.Match(combined.Get(), 3, true); finish();
        require(r.accepted && !r.synchronous && r.eye[0] == 0, "a swapped frame is accepted once from the stale assignment");
        shiftDepth(); capture(4);
        r = matcher.Match(combined.Get(), 4, true); finish();
        require(r.accepted && !r.synchronous && r.proofAge == 1 && r.eye[0] == 1 && r.eye[1] == 0, "the swap is verified one frame later and the assignment follows it");
        expect(1, 0, "corrected assignment assembles the swapped eyes");
        // 4. One changed byte: accepted once by identity, revoked by its own proof,
        //    then rejected by the blocking proof exactly as before the branch.
        combinedPixels.back() ^= 1u << 8; upload(); shiftDepth(); capture(5);
        r = matcher.Match(combined.Get(), 5, true); finish();
        require(r.accepted && !r.synchronous, "a single changed byte passes one frame on identity");
        shiftDepth(); capture(6);
        r = matcher.Match(combined.Get(), 6, true); finish();
        require(!r.accepted && r.synchronous && !matcher.Output() && std::string(r.reason) == "missing or stale correspondence", "the failed proof revokes the assignment and the blocking proof rejects the mismatch");
        require(matcher.HeldView(6, 2) != nullptr && matcher.HeldView(7, 2) != nullptr && matcher.HeldView(8, 2) == nullptr && matcher.HeldView(6, 0) == nullptr, "held depth is bounded by the frame budget");
        assemble(true); upload(); shiftDepth(); capture(7);
        r = matcher.Match(combined.Get(), 7, true); finish();
        require(r.accepted && r.synchronous && r.eye[0] == 1 && r.eye[1] == 0, "the recovery frame proves synchronously");
        expect(1, 0, "the recovery frame assembles exactly");
        // 5. A new resource in place of one candidate forces a blocking proof even in asynchronous mode.
        auto replacement = texture(W, DXGI_FORMAT_R8G8B8A8_UNORM, pixels[0].data());
        assemble(false); upload(); shiftDepth(); capture(8, replacement.Get());
        r = matcher.Match(combined.Get(), 8, true); finish();
        require(r.accepted && r.synchronous && r.eye[0] == 0 && r.eye[1] == 1, "a changed resource identity proves synchronously");
        shiftDepth(); capture(9, replacement.Get());
        r = matcher.Match(combined.Get(), 9, true); finish();
        require(r.accepted && !r.synchronous && r.proofAge == 1, "the new identity is reused once proven");
        // 6. Asynchronous mode off: every frame proves synchronously.
        shiftDepth(); capture(10);
        r = matcher.Match(combined.Get(), 10, false); finish();
        require(r.accepted && r.synchronous && r.eye[0] == 0 && r.eye[1] == 1, "legacy mode stays synchronous");
        // 7. Ambiguity is still rejected: a duplicate candidate cannot be assigned by identity either.
        matcher.Reset();
        require(matcher.Capture(colors[0].Get(), 0, depths[0].Get(), 0, 11, 1) && matcher.Capture(colors[1].Get(), 0, depths[1].Get(), 0, 11, 2) &&
                    matcher.Capture(colors[0].Get(), 0, depths[0].Get(), 0, 11, 3), "duplicate capture");
        r = matcher.Match(combined.Get(), 11, true); finish();
        require(!r.accepted && std::string(r.reason) == "ambiguous correspondence", "a duplicate candidate is ambiguous in both modes");
        protection.ReleaseDevice(d.Get());
        std::cout << "{\"passed\":true,\"checks\":" << checks << ",\"width_per_eye\":" << W << ",\"height\":" << H << "}\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
