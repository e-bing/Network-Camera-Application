#include "../pipeline/EptzCrop.hpp"

#include <cstdio>

namespace
{

    int g_failures = 0;
    constexpr int kMaxPrinted = 20;

    void report(const char *expr, int w, int h, const PtzState &p, const CropRect &r)
    {
        if (++g_failures <= kMaxPrinted)
        {
            std::fprintf(stderr,
                         "FAIL [%s]\n  %dx%d pan=%.2f tilt=%.2f zoom=%.2f -> L%d R%d T%d B%d\n",
                         expr, w, h, p.pan, p.tilt, p.zoom,
                         r.left, r.right, r.top, r.bottom);
        }
    }

#define CHECK_AT(cond, w, h, p, r)     \
    do                                 \
    {                                  \
        if (!(cond))                   \
            report(#cond, w, h, p, r); \
    } while (0)

    // ---- 고정 케이스 ----------------------------------------------------

    void expect_rect(int w, int h, const PtzState &p,
                     int left, int right, int top, int bottom)
    {
        const CropRect r = calculate_crop(w, h, p);
        CHECK_AT(r.left == left && r.right == right &&
                     r.top == top && r.bottom == bottom,
                 w, h, p, r);
    }

    void test_fixed_cases()
    {
        // zoom 1.0: crop 없음 (pan/tilt와 무관)
        expect_rect(1280, 720, {0.0, 0.0, 1.0}, 0, 0, 0, 0);
        expect_rect(1280, 720, {-1.0, -1.0, 1.0}, 0, 0, 0, 0);
        expect_rect(1280, 720, {1.0, 1.0, 1.0}, 0, 0, 0, 0);

        // zoom 2.0: 창 640x360
        expect_rect(1280, 720, {-1.0, 0.0, 2.0}, 0, 640, 180, 180);
        expect_rect(1280, 720, {1.0, 0.0, 2.0}, 640, 0, 180, 180);
        expect_rect(1280, 720, {0.0, 0.0, 2.0}, 320, 320, 180, 180);

        // tilt 방향: 현재 구현은 tilt +1 -> top이 최대(창이 아래로 이동).
        // tilt +1을 "위쪽"으로 바꾸기로 하면 아래 두 줄의 기대값을 뒤집는다.
        expect_rect(1280, 720, {0.0, -1.0, 2.0}, 320, 320, 0, 360);
        expect_rect(1280, 720, {0.0, 1.0, 2.0}, 320, 320, 360, 0);

        // zoom 1.5: 1280/1.5=853.3 -> 짝수 내림 852, 높이 480. 이동 여백 428
        expect_rect(1280, 720, {-1.0, 0.0, 1.5}, 0, 428, 120, 120);
        expect_rect(1280, 720, {1.0, 0.0, 1.5}, 428, 0, 120, 120);
        expect_rect(1280, 720, {0.0, 0.0, 1.5}, 214, 214, 120, 120);
    }

    // ---- 불변식 스윕 ----------------------------------------------------

    void check_invariants(int w, int h, const PtzState &p, int win_w, int win_h)
    {
        const CropRect r = calculate_crop(w, h, p);

        CHECK_AT(r.left >= 0 && r.right >= 0 && r.top >= 0 && r.bottom >= 0,
                 w, h, p, r);
        CHECK_AT(r.left % 2 == 0 && r.right % 2 == 0 &&
                     r.top % 2 == 0 && r.bottom % 2 == 0,
                 w, h, p, r);
        CHECK_AT(r.left + r.right < w && r.top + r.bottom < h, w, h, p, r);

        // 같은 zoom이면 pan/tilt와 무관하게 창 크기는 일정해야 한다
        CHECK_AT(w - r.left - r.right == win_w, w, h, p, r);
        CHECK_AT(h - r.top - r.bottom == win_h, w, h, p, r);
    }

    void sweep_resolution(int w, int h)
    {
        int prev_win_w = w + 2;
        int prev_win_h = h + 2;

        for (int zi = 100; zi <= 200; ++zi)
        {
            const double zoom = zi / 100.0;

            // 기준 창 크기 (pan=tilt=0에서의 결과)
            const PtzState base{0.0, 0.0, zoom};
            const CropRect b = calculate_crop(w, h, base);
            const int win_w = w - b.left - b.right;
            const int win_h = h - b.top - b.bottom;

            // 독립 오라클: 창 크기는 짝수이고 W/zoom 이하, W/zoom-2 초과
            CHECK_AT(win_w % 2 == 0 && win_h % 2 == 0, w, h, base, b);
            CHECK_AT(win_w <= w / zoom + 1e-9 && win_w > w / zoom - 2.0 - 1e-9,
                     w, h, base, b);
            CHECK_AT(win_h <= h / zoom + 1e-9 && win_h > h / zoom - 2.0 - 1e-9,
                     w, h, base, b);

            // zoom이 커질수록 창은 작아지거나 같아야 한다
            CHECK_AT(win_w <= prev_win_w && win_h <= prev_win_h, w, h, base, b);
            prev_win_w = win_w;
            prev_win_h = win_h;

            // pan/tilt 전체 스윕
            for (int ti = -100; ti <= 100; ++ti)
            {
                for (int pi = -100; pi <= 100; ++pi)
                {
                    const PtzState p{pi / 100.0, ti / 100.0, zoom};
                    check_invariants(w, h, p, win_w, win_h);
                }
            }

            // 가장자리 도달 가능성
            const CropRect l = calculate_crop(w, h, {-1.0, 0.0, zoom});
            const CropRect r = calculate_crop(w, h, {1.0, 0.0, zoom});
            const CropRect t = calculate_crop(w, h, {0.0, -1.0, zoom});
            const CropRect bt = calculate_crop(w, h, {0.0, 1.0, zoom});
            CHECK_AT(l.left == 0, w, h, (PtzState{-1.0, 0.0, zoom}), l);
            CHECK_AT(r.right == 0, w, h, (PtzState{1.0, 0.0, zoom}), r);
            CHECK_AT(t.top == 0, w, h, (PtzState{0.0, -1.0, zoom}), t);
            CHECK_AT(bt.bottom == 0, w, h, (PtzState{0.0, 1.0, zoom}), bt);

            // pan/tilt 단조성: 값이 커질수록 창이 같은 방향으로만 이동
            int prev_left = -1;
            int prev_top = -1;
            for (int i = -100; i <= 100; ++i)
            {
                const PtzState pp{i / 100.0, 0.0, zoom};
                const PtzState pt{0.0, i / 100.0, zoom};
                const CropRect rp = calculate_crop(w, h, pp);
                const CropRect rt = calculate_crop(w, h, pt);
                CHECK_AT(rp.left >= prev_left, w, h, pp, rp);
                CHECK_AT(rt.top >= prev_top, w, h, pt, rt);
                prev_left = rp.left;
                prev_top = rt.top;
            }
        }
    }

    // ---- 운영 범위 제약 -------------------------------------------------

    // 실제 파이프라인(1280x720 캡처 -> 640x360 출력)에서 허용 zoom 범위 안의
    // crop 창은 출력보다 양쪽 모두 커야 한다.
    // 같아지면 videoscale이 스트리밍 중 passthrough로 바뀌어
    // v4l2h264enc가 멈추고(IEptzController.hpp 참고), 작아지면 업스케일이 된다.
    // 창 크기가 pan/tilt와 무관하다는 건 check_invariants가 보장하므로
    // 여기서는 pan=tilt=0만 확인한다.
    void test_operating_range_stays_above_output()
    {
        constexpr int kCapW = 1280;
        constexpr int kCapH = 720;
        constexpr int kOutW = 640; // RtspServer.cpp PIPELINE의 출력 caps와 일치해야 함
        constexpr int kOutH = 360;
        constexpr int kSteps = 1000;

        for (int i = 0; i <= kSteps; ++i)
        {
            // 마지막 스텝은 부동소수 오차 없이 정확히 kZoomMax
            const double zoom = (i == kSteps)
                                    ? kZoomMax
                                    : kZoomMin + (kZoomMax - kZoomMin) * i / kSteps;

            const PtzState p{0.0, 0.0, zoom};
            const CropRect r = calculate_crop(kCapW, kCapH, p);
            const int win_w = kCapW - r.left - r.right;
            const int win_h = kCapH - r.top - r.bottom;

            CHECK_AT(win_w > kOutW && win_h > kOutH, kCapW, kCapH, p, r);
        }
    }
} // namespace

int main()
{
    test_fixed_cases();
    test_operating_range_stays_above_output();

    sweep_resolution(1280, 720); // 현재 캡처 해상도
    sweep_resolution(640, 360);
    sweep_resolution(1920, 1080);

    if (g_failures == 0)
    {
        std::puts("test_eptz_crop: all passed");
        return 0;
    }

    std::fprintf(stderr, "test_eptz_crop: %d check(s) failed\n", g_failures);
    return 1;
}