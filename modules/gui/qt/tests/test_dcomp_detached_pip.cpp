// SPDX-License-Identifier: GPL-2.0-or-later
// Native feasibility/regression test, NOT a claim that VLC's vout is integrated.
// Standalone: Qt6::Gui Qt6::Test; d3d11 dxgi dcomp ole32 user32 dwmapi.
// Include modules/gui/qt; enable AUTOMOC; use a Windows SDK >= Windows 8.1.
#ifndef _WIN32_WINNT
# define _WIN32_WINNT 0x0603
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <QGuiApplication>
#include <QTest>
#include <QWindow>
#include "maininterface/dcomp_visual_transfer.hpp"

using Microsoft::WRL::ComPtr;

struct NativeTransferOps {
    IDCompositionDevice *device;
    IDCompositionVisual *mainRoot, *pipRoot, *video, *ui;
    IDCompositionVisual *pipSibling = nullptr;
    HRESULT remove(bool pip) { return (pip ? pipRoot : mainRoot)->RemoveVisual(video); }
    HRESULT add(bool pip) { return (pip ? pipRoot : mainRoot)->AddVisual(video, FALSE, pip ? pipSibling : ui); }
    HRESULT commit() { return device->Commit(); }
};

class TestDCompDetachedPip : public QObject
{
    Q_OBJECT
private slots:
    void actualVisualTransfer()
    {
        // QGuiApplication may already have initialized COM as STA.
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        QVERIFY(SUCCEEDED(com));
        struct ComGuard { ~ComGuard() { CoUninitialize(); } } guard;
        ComPtr<ID3D11Device> gpu;
        ComPtr<ID3D11DeviceContext> context;
        QCOMPARE(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            &gpu, nullptr, &context), S_OK);
        ComPtr<IDXGIDevice> dxgi;
        QCOMPARE(gpu.As(&dxgi), S_OK);
        ComPtr<IDXGIAdapter> adapter;
        QCOMPARE(dxgi->GetAdapter(&adapter), S_OK);
        ComPtr<IDXGIFactory2> factory;
        QCOMPARE(adapter->GetParent(IID_PPV_ARGS(&factory)), S_OK);
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = 64; desc.Height = 64;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        ComPtr<IDXGISwapChain1> content;
        QCOMPARE(factory->CreateSwapChainForComposition(gpu.Get(), &desc, nullptr, &content), S_OK);
        ComPtr<ID3D11Texture2D> buffer;
        QCOMPARE(content->GetBuffer(0, IID_PPV_ARGS(&buffer)), S_OK);
        ComPtr<ID3D11RenderTargetView> view;
        QCOMPARE(gpu->CreateRenderTargetView(buffer.Get(), nullptr, &view), S_OK);
        const float red[] = {1.f, 0.f, 0.f, 1.f};
        // Only buffer zero is renderable. Present rotates its backing storage;
        // clear/present twice to initialize both actual flip-model buffers.
        for (int i = 0; i < 2; ++i)
        {
            context->ClearRenderTargetView(view.Get(), red);
            QCOMPARE(content->Present(0, 0), S_OK);
        }

        ComPtr<IDCompositionDevice> device;
        QCOMPARE(DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&device)), S_OK);
        QWindow main;
        main.setTitle(QStringLiteral("DComp main transfer test"));
        main.setGeometry(40, 40, 160, 120);
        main.create();
        QWindow pip;
        // Independent top-level: never setParent()/setTransientParent(main).
        // Set all native flags before create; changing the main Qt flags may
        // invalidate its QRhi/Qt-owned DComp device in the actual compositor.
        pip.setFlags(Qt::Window | Qt::WindowStaysOnTopHint);
        pip.setTitle(QStringLiteral("DComp detached transfer test"));
        pip.setGeometry(260, 40, 160, 120);
        pip.create();
        const HWND mainHwnd = reinterpret_cast<HWND>(main.winId());
        const HWND pipHwnd = reinterpret_cast<HWND>(pip.winId());
        QVERIFY(mainHwnd != pipHwnd);
        QCOMPARE(GetWindow(pipHwnd, GW_OWNER), HWND(nullptr));
        QVERIFY(GetWindowLongPtrW(pipHwnd, GWL_EXSTYLE) & WS_EX_TOPMOST);
        const LONG_PTR mainExStyle = GetWindowLongPtrW(mainHwnd, GWL_EXSTYLE);
        ComPtr<IDCompositionTarget> mainTarget, pipTarget;
        QCOMPARE(device->CreateTargetForHwnd(mainHwnd, TRUE, &mainTarget), S_OK);
        QCOMPARE(device->CreateTargetForHwnd(pipHwnd, TRUE, &pipTarget), S_OK);
        ComPtr<IDCompositionVisual> mainRoot, pipRoot, ui, video;
        QCOMPARE(device->CreateVisual(&mainRoot), S_OK);
        QCOMPARE(device->CreateVisual(&pipRoot), S_OK);
        QCOMPARE(device->CreateVisual(&ui), S_OK);
        QCOMPARE(device->CreateVisual(&video), S_OK);
        QCOMPARE(mainRoot->AddVisual(ui.Get(), FALSE, nullptr), S_OK);
        QCOMPARE(mainRoot->AddVisual(video.Get(), FALSE, ui.Get()), S_OK);
        QCOMPARE(video->SetContent(content.Get()), S_OK);
        QCOMPARE(mainTarget->SetRoot(mainRoot.Get()), S_OK);
        QCOMPARE(pipTarget->SetRoot(pipRoot.Get()), S_OK);
        QCOMPARE(device->Commit(), S_OK);
        QCOMPARE(device->WaitForCommitCompletion(), S_OK);
        main.show();
        pip.show();
        QCoreApplication::processEvents();
        const LONG_PTR mainStyle = GetWindowLongPtrW(mainHwnd, GWL_STYLE);

        // An already-parented real visual cannot be added a second time.
        // This negative assertion proves transfer actually requires removal.
        QVERIFY(FAILED(pipRoot->AddVisual(video.Get(), FALSE, nullptr)));
        // Invalid sibling on the destination: attachment fails, so restore the
        // original parent before committing. Never leave video ownerless.
        NativeTransferOps ops{device.Get(), mainRoot.Get(), pipRoot.Get(), video.Get(), ui.Get()};
        vlc::DCompVisualTransfer transfer;
        ops.pipSibling = ui.Get();
        QVERIFY(!transfer.move(ops, true));
        QVERIFY(transfer.healthy());
        QVERIFY(!transfer.detached());
        ops.pipSibling = nullptr;
        QCOMPARE(device->WaitForCommitCompletion(), S_OK);
        for (int i = 0; i < 32; ++i)
        {
            QVERIFY(transfer.move(ops, true));
            QVERIFY(transfer.detached());
            QCOMPARE(video->SetOffsetX(0.f), S_OK);
            QCOMPARE(video->SetOffsetY(0.f), S_OK);
            QCOMPARE(device->Commit(), S_OK);
            QCOMPARE(device->WaitForCommitCompletion(), S_OK);
            // An ownerless PiP must survive minimization of the interface.
            main.showMinimized();
            QCoreApplication::processEvents();
            QVERIFY(!IsIconic(pipHwnd));
            QVERIFY(IsWindowVisible(pipHwnd));
            pip.resize(161 + i, 121 + i);
            QCoreApplication::processEvents();
            QVERIFY(transfer.move(ops, false));
            QVERIFY(!transfer.detached());
            QCOMPARE(device->WaitForCommitCompletion(), S_OK);
            main.showNormal();
            QCoreApplication::processEvents();
        }
        QCOMPARE(GetWindowLongPtrW(mainHwnd, GWL_STYLE), mainStyle);
        QCOMPARE(GetWindowLongPtrW(mainHwnd, GWL_EXSTYLE), mainExStyle);
        // Same renderer texture/swapchain stayed alive throughout: no decoder
        // or vout recreation, and the content remains an actual rendered frame.
        D3D11_TEXTURE2D_DESC texture{};
        buffer->GetDesc(&texture);
        texture.Usage = D3D11_USAGE_STAGING;
        texture.BindFlags = 0; texture.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        texture.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        QCOMPARE(gpu->CreateTexture2D(&texture, nullptr, &staging), S_OK);
        context->CopyResource(staging.Get(), buffer.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        QCOMPARE(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), S_OK);
        const quint32 pixel = *static_cast<const quint32 *>(mapped.pData);
        context->Unmap(staging.Get(), 0);
        QCOMPARE(pixel, 0xffff0000u);
        BOOL valid = FALSE;
        QCOMPARE(device->CheckDeviceState(&valid), S_OK);
        QVERIFY(valid);
        // Return video BEFORE destroying the independent target/window.
        QCOMPARE(pipTarget->SetRoot(nullptr), S_OK);
        QCOMPARE(device->Commit(), S_OK);
        QCOMPARE(device->WaitForCommitCompletion(), S_OK);
        pipTarget.Reset();
        pip.destroy();
        QVERIFY(IsWindow(mainHwnd));
        QCOMPARE(mainRoot->RemoveVisual(video.Get()), S_OK);
        QCOMPARE(video->SetContent(nullptr), S_OK);
        QCOMPARE(mainTarget->SetRoot(nullptr), S_OK);
        QCOMPARE(device->Commit(), S_OK);
        QCOMPARE(device->WaitForCommitCompletion(), S_OK);
        mainTarget.Reset();
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    TestDCompDetachedPip test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_dcomp_detached_pip.moc"
