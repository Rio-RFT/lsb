/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */

#include "StdInc.h"
#include <CommCtrl.h>
#include <ctime>
#include <chrono>
#include <random>

#ifdef LAUNCHER_PERSONALITY_MAIN
#include <shobjidl.h>

#include "launcher.rc.h"

#include <ShellScalingApi.h>

#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.Xaml.Media.Animation.h>
#include <winrt/Windows.UI.Composition.h>

#include "CitiLaunch/BackdropBrush.g.h"
#include "winrt/Microsoft.Graphics.Canvas.Effects.h"

#include <DirectXMath.h>
#include <roapi.h>

#include <CfxState.h>
#include <HostSharedData.h>

#include <boost/algorithm/string.hpp>

#include <d2d1effects.h>
#include <d2d1_1.h>
#pragma comment(lib, "dxguid.lib")

#include <windows.graphics.effects.h>
#include <windows.graphics.effects.interop.h>

#pragma comment(lib, "runtimeobject.lib")
#pragma comment(lib, "delayimp.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "shcore.lib")

using namespace ABI::Windows::Graphics::Effects;

struct CompositionEffect : winrt::implements
	<
		CompositionEffect,
		winrt::Windows::Graphics::Effects::IGraphicsEffectSource, 
		winrt::Windows::Graphics::Effects::IGraphicsEffect,
		ABI::Windows::Graphics::Effects::IGraphicsEffectD2D1Interop
	>
{
	CompositionEffect(const GUID& effectId)
	{
		m_effectId = effectId;
	}

	winrt::hstring Name()
	{
		return m_name;
	}

	void Name(winrt::hstring const& name)
	{
		m_name = name;
	}

	template<typename T>
	void SetProperty(const std::string& name, const T& value, GRAPHICS_EFFECT_PROPERTY_MAPPING mapping = GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT)
	{
		m_properties.emplace_back(name, winrt::box_value(value), mapping);
	}

	template<int N>
	void SetProperty(const std::string& name, const float (&value)[N], GRAPHICS_EFFECT_PROPERTY_MAPPING mapping = GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT)
	{
		const float* valuePointerStart = &value[0];
		const float* valuePointerEnd = valuePointerStart + N;
		m_properties.emplace_back(name, winrt::Windows::Foundation::PropertyValue::CreateSingleArray(winrt::array_view<const float>{ valuePointerStart, valuePointerEnd }), mapping);
	}

	template<>
	void SetProperty(const std::string& name, const winrt::Windows::UI::Color& color, GRAPHICS_EFFECT_PROPERTY_MAPPING mapping)
	{
		float values[] = { color.R / 255.0f, color.G / 255.0f, color.B / 255.0f, color.A / 255.0f };
		SetProperty(name, values, mapping);
	}

	template<>
	void SetProperty(const std::string& name, const winrt::Microsoft::Graphics::Canvas::Effects::Matrix5x4& value, GRAPHICS_EFFECT_PROPERTY_MAPPING mapping)
	{
		float mat[5 * 4];
		memcpy(mat, &value, sizeof(mat));
		SetProperty(name, mat, mapping);
	}

	template<>
	void SetProperty(const std::string& name, const winrt::Windows::Foundation::Numerics::float3x2& value, GRAPHICS_EFFECT_PROPERTY_MAPPING mapping)
	{
		float mat[3 * 2];
		memcpy(mat, &value, sizeof(mat));
		SetProperty(name, mat, mapping);
	}

	void AddSource(const winrt::Windows::Graphics::Effects::IGraphicsEffectSource& source)
	{
		m_sources.push_back(source);
	}

	virtual HRESULT __stdcall GetEffectId(GUID* id) override
	{
		*id = m_effectId;
		return S_OK;
	}

	virtual HRESULT __stdcall GetNamedPropertyMapping(LPCWSTR name, UINT* index, GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) override
	{
		auto nname = ToNarrow(name);

		auto entry = std::find_if(m_properties.begin(), m_properties.end(), [&nname](const auto& property)
		{
			return nname == std::get<0>(property);
		});

		if (entry != m_properties.end())
		{
			*index = entry - m_properties.begin();
			*mapping = std::get<2>(*entry);
			return S_OK;
		}

		return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
	}

	virtual HRESULT __stdcall GetPropertyCount(UINT* count) override
	{
		*count = m_properties.size();
		return S_OK;
	}

	virtual HRESULT __stdcall GetProperty(UINT index, ABI::Windows::Foundation::IPropertyValue** value) override
	{
		std::get<1>(m_properties[index]).as<ABI::Windows::Foundation::IPropertyValue>().copy_to(value);
		return S_OK;
	}

	virtual HRESULT __stdcall GetSource(UINT index, ABI::Windows::Graphics::Effects::IGraphicsEffectSource** source) override
	{
		m_sources[index].as<ABI::Windows::Graphics::Effects::IGraphicsEffectSource>().copy_to(source);
		return S_OK;
	}

	virtual HRESULT __stdcall GetSourceCount(UINT* count) override
	{
		*count = UINT(m_sources.size());
		return S_OK;
	}

private:
	GUID m_effectId;

	winrt::hstring m_name = L"";

	std::vector<std::tuple<std::string, winrt::Windows::Foundation::IInspectable, GRAPHICS_EFFECT_PROPERTY_MAPPING>> m_properties;
	std::vector<winrt::Windows::Graphics::Effects::IGraphicsEffectSource> m_sources;
};

static class DPIScaler
{
public:
	DPIScaler()
	{
		// Default DPI is 96 (100%)
		dpiX = 96;
		dpiY = 96;
	}

	void SetScale(UINT dpiX, UINT dpiY)
	{
		this->dpiX = dpiX;
		this->dpiY = dpiY;
	}

	int ScaleX(int x)
	{
		return MulDiv(x, dpiX, 96);
	}

	int ScaleY(int y)
	{
		return MulDiv(y, dpiY, 96);
	}

private:
	UINT dpiX, dpiY;
} g_dpi;

using namespace winrt::Windows::UI;
using namespace winrt::Windows::UI::Composition;
using namespace winrt::Windows::UI::Xaml::Hosting;
using namespace winrt::Windows::Foundation::Numerics;

struct TenUI
{
	DesktopWindowXamlSource uiSource{ nullptr };

	winrt::Windows::UI::Xaml::UIElement snailContainer{ nullptr };
	winrt::Windows::UI::Xaml::Controls::TextBlock topStatic{ nullptr };
	winrt::Windows::UI::Xaml::Controls::TextBlock bottomStatic{ nullptr };
	winrt::Windows::UI::Xaml::Controls::Border progressBar{ nullptr };
	
	// Parallax effect elements
	winrt::Windows::UI::Xaml::Controls::Grid backdropGrid{ nullptr };
	winrt::Windows::UI::Composition::CompositionEffectBrush effectBrush{ nullptr };
	winrt::Windows::Foundation::Numerics::float3x2 baseTransform{};
};

//static thread_local struct  
static struct
{
	HWND rootWindow;
	HWND topStatic;
	HWND bottomStatic;
	HWND progressBar;
	HWND cancelButton;

	HWND tenWindow;

	UINT taskbarMsg;

	bool tenMode;
	bool canceled;

	std::unique_ptr<TenUI> ten;

	ITaskbarList3* tbList;

	wchar_t topText[512];
	wchar_t bottomText[512];
} g_uui;

HWND UI_GetWindowHandle()
{
	return g_uui.rootWindow;
}

HFONT UI_CreateScaledFont(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic,
	DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision,
	DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName)
{
	LOGFONT logFont;
	
	memset(&logFont, 0, sizeof(LOGFONT));
	logFont.lfHeight = g_dpi.ScaleY(cHeight);
	logFont.lfWidth = cWidth;
	logFont.lfEscapement = cEscapement;
	logFont.lfOrientation = cOrientation;
	logFont.lfWeight = cWeight;
	logFont.lfItalic = bItalic;
	logFont.lfUnderline = bUnderline;
	logFont.lfStrikeOut = bStrikeOut;
	logFont.lfCharSet = iCharSet;
	logFont.lfOutPrecision = 8;
	logFont.lfClipPrecision = iClipPrecision;
	logFont.lfQuality = iQuality;
	logFont.lfPitchAndFamily = iPitchAndFamily;
	wcscpy_s(logFont.lfFaceName, pszFaceName);
	return CreateFontIndirect(&logFont);
}

static std::wstring g_mainXaml = LR"(
<Grid Background="Transparent"
    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
    xmlns:d="http://schemas.microsoft.com/expression/blend/2008"
    xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006"
	xmlns:local="using:CitiLaunch"
    mc:Ignorable="d">

    <Border CornerRadius="12" Width="854" Height="480">
        <Border.Background>
            <SolidColorBrush Color="#161923"/>
        </Border.Background>
        <Grid>
            <Grid.Clip>
                <RectangleGeometry Rect="0,0,854,480" RadiusX="12" RadiusY="12"/>
            </Grid.Clip>
            <Grid x:Name="BackdropGrid" />
		    <SwapChainPanel x:Name="Overlay" />
		
		<!-- Christmas String Lights - Only visible in Dec/Jan -->
		<Canvas x:Name="ChristmasLights" Visibility="Collapsed">
			<!-- Top wire -->
			<Path Stroke="#222222" StrokeThickness="2" Data="M -10,18 Q 60,5 107,22 Q 160,42 214,18 Q 270,0 321,28 Q 375,52 428,22 Q 480,0 535,32 Q 590,55 642,22 Q 695,0 749,28 Q 800,48 854,18 Q 900,5 864,22"/>
			<!-- Top bulbs -->
			<Ellipse Canvas.Left="103" Canvas.Top="20" Width="14" Height="20" Fill="#ff4444" x:Name="bulb1"/>
			<Ellipse Canvas.Left="210" Canvas.Top="16" Width="14" Height="20" Fill="#44ff44" x:Name="bulb2"/>
			<Ellipse Canvas.Left="317" Canvas.Top="26" Width="14" Height="20" Fill="#4488ff" x:Name="bulb3"/>
			<Ellipse Canvas.Left="424" Canvas.Top="20" Width="14" Height="20" Fill="#ffcc00" x:Name="bulb4"/>
			<Ellipse Canvas.Left="531" Canvas.Top="30" Width="14" Height="20" Fill="#ff6600" x:Name="bulb5"/>
			<Ellipse Canvas.Left="638" Canvas.Top="20" Width="14" Height="20" Fill="#ff4444" x:Name="bulb6"/>
			<Ellipse Canvas.Left="745" Canvas.Top="26" Width="14" Height="20" Fill="#44ff44" x:Name="bulb7"/>
			<!-- Bottom wire -->
			<Path Stroke="#222222" StrokeThickness="2" Data="M -10,458 Q 60,472 107,455 Q 160,438 214,462 Q 270,480 321,455 Q 375,432 428,458 Q 480,480 535,452 Q 590,428 642,458 Q 695,480 749,452 Q 800,432 864,458"/>
			<!-- Bottom bulbs (upside down) -->
			<Ellipse Canvas.Left="103" Canvas.Top="440" Width="14" Height="20" Fill="#ffcc00" x:Name="bulb8"/>
			<Ellipse Canvas.Left="210" Canvas.Top="448" Width="14" Height="20" Fill="#ff6600" x:Name="bulb9"/>
			<Ellipse Canvas.Left="317" Canvas.Top="438" Width="14" Height="20" Fill="#ff4444" x:Name="bulb10"/>
			<Ellipse Canvas.Left="424" Canvas.Top="448" Width="14" Height="20" Fill="#44ff44" x:Name="bulb11"/>
			<Ellipse Canvas.Left="531" Canvas.Top="436" Width="14" Height="20" Fill="#4488ff" x:Name="bulb12"/>
			<Ellipse Canvas.Left="638" Canvas.Top="448" Width="14" Height="20" Fill="#ffcc00" x:Name="bulb13"/>
			<Ellipse Canvas.Left="745" Canvas.Top="436" Width="14" Height="20" Fill="#ff6600" x:Name="bulb14"/>
		</Canvas>
		
		<!-- Snowflakes container -->
		<Canvas x:Name="SnowflakesCanvas" Visibility="Collapsed"/>
		
        <StackPanel Orientation="Vertical" VerticalAlignment="Center">)"
#if defined(GTA_FIVE)
	R"(
            <Viewbox Height="150" Margin="0,0,0,20">
                <Canvas Width="1000" Height="1000">
                    <Path x:Name="LogoPath" Fill="White" Data="M702.52 451.73L767 401.74c3.31-2.56 5.44-6.32 5.96-10.46l35.13-284.6c1.19-9.64-6.56-18.24-16.44-18.24h-448.6c-14.53 0-21.47 17.36-10.96 27.43l82.18 78.79c5.46 5.23 5.63 13.77 0.39 19.03v0c-5.21 5.22-13.79 5.24-19.21 0.05l-83.69-80.24c-9.68-9.29-25.78-3.89-27.39 9.19l-39.66 321.3c-0.62 5.01 1.2 10.05 4.91 13.61l64.9 62.23c3.1 2.97 7.24 4.64 11.52 4.64h243.12c9.19 0 18.07 3.6 24.69 10.01l0 0c7.82 7.57 11.64 18.26 10.33 28.87l-23.42 189.71c-0.98 7.93-7.75 13.83-15.87 13.83l-195.71 0c-9.88 0-17.63-8.6-16.44-18.24l16.24-131.56c0.62-5.01-1.2-10.05-4.91-13.61L263.4 526.92c-9.68-9.29-25.78-3.89-27.39 9.19l-44.1 357.21c-1.19 9.64 6.56 18.24 16.44 18.24h486.61c8.12 0 14.89-5.9 15.87-13.83l18.08-146.49l21.76-176.27c0.62-5.01-1.2-10.05-4.91-13.61l-49.09-47.07c-5.9-5.66-15.07-6.2-21.44-1.27l-43.73 33.9c0 0-3.49 2.8-7.94 2.49c-5.13-0.36-7.87-3.39-7.87-3.39s-1.26-1.15-2.08-2.63c-1.05-1.88-2.04-4.01-1.61-7.56c0.49-4.04 3.56-6.88 3.56-6.88l45.64-35.38c7.66-5.94 8.15-17.25 1.04-24.07l-47.06-45.11c-3.1-2.97-7.24-4.64-11.52-4.64H414.31c-9.88 0-17.63-8.6-16.44-18.24l18.73-151.75c1.13-9.15 5.26-17.63 11.76-24.17l0 0c7.95-7.99 18.85-12.48 30.3-12.48h172.14c9.88 0 17.63 8.6 16.44 18.24l-19.62 158.93c-0.62 5.01 1.2 10.05 4.91 13.61l48.54 46.54C686.98 456.12 696.15 456.66 702.52 451.73z" />
                </Canvas>
	)"
#elif defined(IS_RDR3)
	R"(
			<Viewbox Height="150" Margin="0,0,0,15">
				<Grid>
				<Path Data="F1 M 38.56,38.56 L 779.52,38.56 779.52,1019.52 38.56,1019.52 z"  Fill="#00000000" />
				<Path Data="F1 M 153.23,78.44 L 154.67,77.16 153.23,75.72 153.23,78.44 153.23,78.44 z"  Fill="#ffffffff" />
				<Path Data="F1 M 677.12,48.82 L 523.2,98.61 516.32,118.63 673.43,67.71 677.12,48.82 677.12,48.82 z"  Fill="#ffffffff" />
				<Path Data="F1 M 666.07,105.5 L 668.63,92.37 507.35,144.73 502.7,158.34 666.07,105.5 666.07,105.5 z"  Fill="#ffffffff" />
				<Path Data="F1 M 0,0 L -13.94,40.99 -32.52,105.83 -42.61,153.07 116.91,176.77 134.69,107.28 166.24,-53.8
					 0,0 0.16,0 z" RenderTransform="1,0,0,1,496.62,175.63" Fill="#ffffffff" />
				<Path Data="F1 M 670.55,38.73 L 543.7,38.73 527.85,84.84 670.55,38.73 z"  Fill="#ffffffff" />
				<Path Data="F1 M 311.47,167.46 L 152.43,218.86 151.95,224.46 151.95,236.79 310.51,185.55 311.47,167.46 z"  Fill="#ffffffff" />
				<Path Data="F1 M 308.91,221.26 L 309.55,209.09 151.95,260.01 151.95,272.18 308.91,221.26 308.91,221.26 z"  Fill="#ffffffff" />
				<Path Data="F1 M 0,0 L -9.45,-0.96 19.22,-146.18 -133.89,-168.92 -144.78,-118.16 -164.64,-6.72 -266.82,-6.72
					 -245.52,-296.05 -245.52,-404.93 -244.72,-425.59 -401.04,-375.15 -401.04,-265.63 -405.2,-256.34 -404.88,-247.7
					 -409.05,-182.05 -412.09,-168.76 -411.77,-133.06 -411.77,358.34 94.18,358.34 94.18,326.48 116.76,-5.28
					 34.44,0 0,0 0,0 z
					M -25.14,211.04 L -272.27,211.04 -264.26,171.33 -264.26,143.31 -272.27,124.73 -25.14,124.73 -27.87,163.16
					 -25.14,211.04 z" RenderTransform="1,0,0,1,552.99,662.7" Fill="#ffffffff" />
				<Path Data="F1 M 0,0 L 2.88,-108.56 -156.31,-113.84 -155.83,-101.99 -157.12,-79.41 -157.12,37.47 -158.24,51.08
					 0,0 0,0 z" RenderTransform="1,0,0,1,311.79,155.13" Fill="#ffffffff" />
				</Grid>
)"
#elif defined(GTA_NY)
								 R"(
			<Viewbox Height="150" Margin="0,0,0,15">
				<Grid>
				<Path Data="M26,145L54.571,145C54.952,144.905 55.143,144.714 55.143,144.429L55.143,69C43.714,57.286 33.905,47.476 25.714,39.571L25.429,39.571L25.429,144.429C25.524,144.81 25.714,145 26,145ZM54.857,57.857L55.143,57.857L55.143,54.143C43.714,42.429 33.905,32.619 25.714,24.714L25.429,24.714L25.429,28.429C36.857,40.048 46.667,49.857 54.857,57.857ZM54.857,43L55.143,43L55.143,31.571C46.857,23 38,14.143 28.571,5L26,5C25.619,5 25.429,5.19 25.429,5.571L25.429,13.571C36.857,25.19 46.667,35 54.857,43ZM57.714,30.429L124,30.429C124.381,30.333 124.571,30.143 124.571,29.857L124.571,5.571C124.571,5.19 124.381,5 124,5L32.571,5L32.571,5.286C41.714,14.619 50.095,23 57.714,30.429Z"  Fill="#ffffffff" />
				</Grid>
)"
#endif
R"(         </Viewbox>
            <TextBlock x:Name="static1" Text=" " TextAlignment="Center" Foreground="#ffffffff" FontSize="24" FontWeight="SemiBold" />
			<Grid Margin="0,15,0,15" Width="350" Height="10">
				<Border CornerRadius="5" Background="#33ff81ff" />
				<Border x:Name="progressBar" CornerRadius="5" Background="#00d4ff" HorizontalAlignment="Left" Width="0" />
			</Grid>
            <TextBlock x:Name="static2" Text=" " TextAlignment="Center" Foreground="#ffeeeeee" FontSize="16" />
			<StackPanel Orientation="Horizontal" HorizontalAlignment="Center" x:Name="snailContainer" Visibility="Collapsed">
				<TextBlock TextAlignment="Center" Foreground="#ddeeeeee" FontSize="14" Width="430" TextWrapping="Wrap">
					🐌 RedM game storage downloads are peer-to-peer and may be slower than usual downloads. Please be patient.
				</TextBlock>
			</StackPanel>
        </StackPanel>
        </Grid>
    </Border>
</Grid>
)";

struct BackdropBrush : winrt::CitiLaunch::implementation::BackdropBrushT<BackdropBrush>
{
	BackdropBrush() = default;

	void OnConnected();
	void OnDisconnected();

	winrt::Windows::UI::Composition::CompositionPropertySet ps{ nullptr };
};

void BackdropBrush::OnConnected()
{
	if (!CompositionBrush())
	{
		//
		// !NOTE! if trying to change the following code (add extra effects, change effects, etc.)
		// 
		// CLSIDs and properties are from Win2D:
		//   https://github.com/microsoft/Win2D/tree/99ce19f243c6a6332f0ea312cd29fc3c785a540b/winrt/lib/effects/generated
		//
		// The .h files show the CLSID used, the .cpp files the properties with names, type, mapping and default values.
		// 
		// Properties *have* to be set in the original order - initial deserialization (at least in wuceffects.dll 10.0.22000)
		// will check all properties before checking the name mapping. Also, `Source` properties are mapped to the AddSource
		// list, instead of being a real property.
		//
		auto effect = CompositionEffect(CLSID_D2D1Flood);

#ifdef GTA_FIVE
		effect.SetProperty("Color", winrt::Windows::UI::ColorHelper::FromArgb(255, 0x16, 0x19, 0x23));
#elif defined(IS_RDR3)
		effect.SetProperty("Color", winrt::Windows::UI::ColorHelper::FromArgb(255, 186, 2, 2));
#elif defined(GTA_NY)
		effect.SetProperty("Color", winrt::Windows::UI::ColorHelper::FromArgb(255, 0x4D, 0xA6, 0xD3));
#endif

		winrt::Windows::UI::Composition::CompositionEffectSourceParameter sp{ L"layer" };
		winrt::Windows::UI::Composition::CompositionEffectSourceParameter sp2{ L"rawImage" };

		auto mat2d = winrt::Windows::Foundation::Numerics::float3x2{};

		using namespace DirectX;
		// Rotate 30 degrees (0.5236 radians) around center
		constexpr float rotationAngle = 0.5236f; // 30 degrees in radians
		auto matrix = XMMatrixTransformation2D(
			XMVectorSet(500.0f, 500.0f, 0.0f, 0.0f), // Scale center at backdrop center (1000x1000 / 2)
			0.0f, // No rotation for scale
			XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f), // Scale (1:1)
			XMVectorSet(500.0f, 500.0f, 0.0f, 0.0f), // Rotation center
			rotationAngle, // 30 degrees rotation
			XMVectorSet(-250.0f, -150.0f, 0.0f, 1.0f) // Translation offset
		);
		XMStoreFloat3x2(&mat2d, matrix);

		// Store base transform for parallax effect
		if (g_uui.ten)
		{
			g_uui.ten->baseTransform = mat2d;
		}

		auto layer = CompositionEffect(CLSID_D2D12DAffineTransform);
		layer.AddSource(sp2);
		layer.Name(L"xform");

		layer.SetProperty("InterpolationMode", uint32_t(D2D1_2DAFFINETRANSFORM_INTERPOLATION_MODE_LINEAR));
		layer.SetProperty("BorderMode", uint32_t(D2D1_BORDER_MODE_SOFT));
		layer.SetProperty("TransformMatrix", mat2d);
		layer.SetProperty("Sharpness", 0.0f);

		auto mat = winrt::Microsoft::Graphics::Canvas::Effects::Matrix5x4();
		memset(&mat, 0, sizeof(mat));
		mat.M44 = 1.0f;

#ifdef GTA_FIVE
		mat.M11 = 1.0f;
		mat.M22 = 1.0f;
		mat.M33 = 1.0f;
		mat.M44 = 0.03f;
#elif defined(IS_RDR3) || defined(GTA_NY)
		mat.M11 = 1.0f;
		mat.M22 = 1.0f;
		mat.M33 = 1.0f;
		mat.M44 = 0.15f;
#endif

		auto layerColor = CompositionEffect(CLSID_D2D1ColorMatrix);
		layerColor.AddSource(layer);
		layerColor.SetProperty("ColorMatrix", mat);
		layerColor.SetProperty("AlphaMode", uint32_t(D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED), GRAPHICS_EFFECT_PROPERTY_MAPPING_COLORMATRIX_ALPHA_MODE);
		layerColor.SetProperty("ClampOutput", false);

		auto compEffect = CompositionEffect(CLSID_D2D1Composite);
		compEffect.SetProperty("Mode", uint32_t(D2D1_COMPOSITE_MODE_SOURCE_OVER));
		compEffect.AddSource(effect);
		compEffect.AddSource(layerColor);

		auto hRsc = FindResource(GetModuleHandle(NULL), MAKEINTRESOURCE(IDM_BACKDROP), L"MEOW");
		auto resSize = SizeofResource(GetModuleHandle(NULL), hRsc);
		auto resData = LoadResource(GetModuleHandle(NULL), hRsc);

		auto resPtr = static_cast<const uint8_t*>(LockResource(resData));

		auto iras = winrt::Windows::Storage::Streams::InMemoryRandomAccessStream();
		auto dw = winrt::Windows::Storage::Streams::DataWriter{ iras };
		dw.WriteBytes(winrt::array_view<const uint8_t>{resPtr, resPtr + resSize});

		auto iao = dw.StoreAsync();
		while (iao.Status() != winrt::Windows::Foundation::AsyncStatus::Completed)
		{
			Sleep(0);
		}

		iras.Seek(0);

		auto surf = winrt::Windows::UI::Xaml::Media::LoadedImageSurface::StartLoadFromStream(iras);

		auto cb = winrt::Windows::UI::Xaml::Window::Current().Compositor().CreateSurfaceBrush();
		cb.Surface(surf);
		cb.Stretch(winrt::Windows::UI::Composition::CompositionStretch::None);

		auto ef = winrt::Windows::UI::Xaml::Window::Current().Compositor().CreateEffectFactory(compEffect, { L"xform.TransformMatrix" });
		auto eb = ef.CreateBrush();
		eb.SetSourceParameter(L"rawImage", cb);

		// Add moving animation (restored from original)
		using namespace std::chrono_literals;

		auto kfa = winrt::Windows::UI::Xaml::Window::Current().Compositor().CreateVector2KeyFrameAnimation();
		kfa.InsertKeyFrame(0.0f, { 0.0f, 0.0f });
		kfa.InsertKeyFrame(0.25f, { 0.0f, -200.0f }, winrt::Windows::UI::Xaml::Window::Current().Compositor().CreateLinearEasingFunction());
		kfa.InsertKeyFrame(0.5f, { -200.0f, -200.0f }, winrt::Windows::UI::Xaml::Window::Current().Compositor().CreateLinearEasingFunction());
		kfa.InsertKeyFrame(0.75f, { -200.0f, 0.0f }, winrt::Windows::UI::Xaml::Window::Current().Compositor().CreateLinearEasingFunction());
		kfa.InsertKeyFrame(1.0f, { 0.0f, 0.0f }, winrt::Windows::UI::Xaml::Window::Current().Compositor().CreateLinearEasingFunction());
		kfa.Duration(60s);
		kfa.IterationBehavior(winrt::Windows::UI::Composition::AnimationIterationBehavior::Forever);
		kfa.Target(L"xlate");

		auto ag = winrt::Windows::UI::Xaml::Window::Current().Compositor().CreateAnimationGroup();
		ag.Add(kfa);

		ps = winrt::Windows::UI::Xaml::Window::Current().Compositor().CreatePropertySet();
		ps.InsertVector2(L"xlate", { 0.0f, 0.0f });
		ps.StartAnimationGroup(ag);

		auto ca = winrt::Windows::UI::Xaml::Window::Current().Compositor().CreateExpressionAnimation();
		ca.SetReferenceParameter(L"ps", ps);
		ca.SetMatrix3x2Parameter(L"rot", mat2d);
		ca.Expression(L"Matrix3x2.CreateFromTranslation(ps.xlate) * rot");

		eb.StartAnimation(L"xform.TransformMatrix", ca);

		// Store effect brush for parallax updates
		if (g_uui.ten)
		{
			g_uui.ten->effectBrush = eb;
		}

		CompositionBrush(eb);
	}
}

void BackdropBrush::OnDisconnected()
{
	if (CompositionBrush())
	{
		CompositionBrush(nullptr);
	}
}

#include <wrl.h>
#include <d3d11.h>
#include <dxgi1_4.h>

#include <windows.ui.xaml.media.dxinterop.h>

using Microsoft::WRL::ComPtr;

const BYTE g_PixyShader[] =
{
     68,  88,  66,  67, 115,  61, 
    165, 134, 202, 176,  67, 148, 
    204, 160, 214, 207, 231, 188, 
    224, 101,   1,   0,   0,   0, 
     48,  10,   0,   0,   5,   0, 
      0,   0,  52,   0,   0,   0, 
     36,   1,   0,   0, 124,   1, 
      0,   0, 176,   1,   0,   0, 
    180,   9,   0,   0,  82,  68, 
     69,  70, 232,   0,   0,   0, 
      1,   0,   0,   0,  68,   0, 
      0,   0,   1,   0,   0,   0, 
     28,   0,   0,   0,   0,   4, 
    255, 255,   0,   1,   0,   0, 
    192,   0,   0,   0,  60,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,   1,   0, 
      0,   0,   1,   0,   0,   0, 
     80, 115,  67,  98, 117, 102, 
      0, 171,  60,   0,   0,   0, 
      2,   0,   0,   0,  92,   0, 
      0,   0,  16,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0, 140,   0,   0,   0, 
      0,   0,   0,   0,   8,   0, 
      0,   0,   0,   0,   0,   0, 
    152,   0,   0,   0,   0,   0, 
      0,   0, 168,   0,   0,   0, 
      8,   0,   0,   0,   4,   0, 
      0,   0,   2,   0,   0,   0, 
    176,   0,   0,   0,   0,   0, 
      0,   0, 105,  82, 101, 115, 
    111, 108, 117, 116, 105, 111, 
    110,   0,   1,   0,   3,   0, 
      1,   0,   2,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
    105,  84, 105, 109, 101,   0, 
    171, 171,   0,   0,   3,   0, 
      1,   0,   1,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
     77, 105,  99, 114, 111, 115, 
    111, 102, 116,  32,  40,  82, 
     41,  32,  72,  76,  83,  76, 
     32,  83, 104,  97, 100, 101, 
    114,  32,  67, 111, 109, 112, 
    105, 108, 101, 114,  32,  49, 
     48,  46,  49,   0,  73,  83, 
     71,  78,  80,   0,   0,   0, 
      2,   0,   0,   0,   8,   0, 
      0,   0,  56,   0,   0,   0, 
      0,   0,   0,   0,   1,   0, 
      0,   0,   3,   0,   0,   0, 
      0,   0,   0,   0,  15,   0, 
      0,   0,  68,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   3,   0,   0,   0, 
      1,   0,   0,   0,   3,   3, 
      0,   0,  83,  86,  95,  80, 
     79,  83,  73,  84,  73,  79, 
     78,   0,  84,  69,  88,  67, 
     79,  79,  82,  68,   0, 171, 
    171, 171,  79,  83,  71,  78, 
     44,   0,   0,   0,   1,   0, 
      0,   0,   8,   0,   0,   0, 
     32,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      3,   0,   0,   0,   0,   0, 
      0,   0,  15,   0,   0,   0, 
     83,  86,  95,  84,  65,  82, 
     71,  69,  84,   0, 171, 171, 
     83,  72,  68,  82, 252,   7, 
      0,   0,  64,   0,   0,   0, 
    255,   1,   0,   0,  89,   0, 
      0,   4,  70, 142,  32,   0, 
      0,   0,   0,   0,   1,   0, 
      0,   0,  98,  16,   0,   3, 
     50,  16,  16,   0,   1,   0, 
      0,   0, 101,   0,   0,   3, 
    242,  32,  16,   0,   0,   0, 
      0,   0, 104,   0,   0,   2, 
      5,   0,   0,   0,  56,   0, 
      0,  11,  50,   0,  16,   0, 
      0,   0,   0,   0, 166, 138, 
     32,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,   2,  64, 
      0,   0, 205, 204, 204,  61, 
     66,  96, 101,  63,   0,   0, 
      0,   0,   0,   0,   0,   0, 
     77,   0,   0,   6,  18,   0, 
     16,   0,   0,   0,   0,   0, 
      0, 208,   0,   0,  10,   0, 
     16,   0,   0,   0,   0,   0, 
     50,   0,   0,  15, 194,   0, 
     16,   0,   0,   0,   0,   0, 
     86,  17,  16,   0,   1,   0, 
      0,   0,   2,  64,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   0,   0, 128, 191, 
      0,   0, 128,  63,   2,  64, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
    128,  63,   0,   0,   0,   0, 
     54,   0,   0,   8,  50,   0, 
     16,   0,   1,   0,   0,   0, 
      2,  64,   0,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,  48,   0,   0,   1, 
     33,   0,   0,   7,  66,   0, 
     16,   0,   1,   0,   0,   0, 
     26,   0,  16,   0,   1,   0, 
      0,   0,   1,  64,   0,   0, 
     66,   0,   0,   0,   3,   0, 
      4,   3,  42,   0,  16,   0, 
      1,   0,   0,   0,  43,   0, 
      0,   5,  66,   0,  16,   0, 
      1,   0,   0,   0,  26,   0, 
     16,   0,   1,   0,   0,   0, 
     56,   0,   0,   7, 130,   0, 
     16,   0,   1,   0,   0,   0, 
     42,   0,  16,   0,   1,   0, 
      0,   0,   1,  64,   0,   0, 
     53, 165, 231,  64,  50,   0, 
      0,  15, 114,   0,  16,   0, 
      2,   0,   0,   0, 166,  10, 
     16,   0,   1,   0,   0,   0, 
      2,  64,   0,   0,   0,   0, 
      0,  63, 143, 194, 117,  60, 
     10, 215,  35,  60,   0,   0, 
      0,   0,   2,  64,   0,   0, 
      0,   0, 128,  63,   0,   0, 
    128,  63,   0,   0, 128,  63, 
      0,   0,   0,   0,  56,   0, 
      0,   7, 130,   0,  16,   0, 
      2,   0,   0,   0,  42,   0, 
     16,   0,   0,   0,   0,   0, 
     10,   0,  16,   0,   2,   0, 
      0,   0,  65,   0,   0,   5, 
    130,   0,  16,   0,   1,   0, 
      0,   0,  58,   0,  16,   0, 
      1,   0,   0,   0,  50,   0, 
      0,  10, 130,   0,  16,   0, 
      1,   0,   0,   0,  42,   0, 
     16,   0,   1,   0,   0,   0, 
      1,  64,   0,   0,  53, 165, 
    231,  64,  58,   0,  16, 128, 
     65,   0,   0,   0,   1,   0, 
      0,   0,  50,   0,   0,  10, 
     18,   0,  16,   0,   3,   0, 
      0,   0,  42, 128,  32,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   1,  64,   0,   0, 
      0,   0,   0,  64,  42,   0, 
     16,   0,   1,   0,   0,   0, 
     77,   0,   0,   6,  18,   0, 
     16,   0,   3,   0,   0,   0, 
      0, 208,   0,   0,  10,   0, 
     16,   0,   3,   0,   0,   0, 
     50,   0,   0,  10, 130,   0, 
     16,   0,   1,   0,   0,   0, 
     10,   0,  16, 128,  65,   0, 
      0,   0,   3,   0,   0,   0, 
      1,  64,   0,   0, 205, 204, 
    204,  61,  58,   0,  16,   0, 
      1,   0,   0,   0,  56,   0, 
      0,   7,  34,   0,  16,   0, 
      3,   0,   0,   0,  58,   0, 
     16,   0,   1,   0,   0,   0, 
     58,   0,  16,   0,   2,   0, 
      0,   0,  14,   0,   0,   7, 
     18,   0,  16,   0,   3,   0, 
      0,   0,  26,   0,  16,   0, 
      0,   0,   0,   0,  26,   0, 
     16,   0,   2,   0,   0,   0, 
     50,   0,   0,   9,  50,   0, 
     16,   0,   2,   0,   0,   0, 
    230,  10,  16,   0,   0,   0, 
      0,   0,   6,   0,  16,   0, 
      2,   0,   0,   0,  70,   0, 
     16,   0,   3,   0,   0,   0, 
     65,   0,   0,   5,  50,   0, 
     16,   0,   3,   0,   0,   0, 
     22,   5,  16,   0,   2,   0, 
      0,   0,   0,   0,   0,   7, 
    130,   0,  16,   0,   1,   0, 
      0,   0,  42,   0,  16,   0, 
      1,   0,   0,   0,   1,  64, 
      0,   0,  18, 131, 249,  65, 
     65,   0,   0,   5,  66,   0, 
     16,   0,   3,   0,   0,   0, 
     58,   0,  16,   0,   1,   0, 
      0,   0,  50,   0,   0,  15, 
    114,   0,  16,   0,   4,   0, 
      0,   0,  70,   2,  16,   0, 
      3,   0,   0,   0,   2,  64, 
      0,   0, 172, 197,  39,  55, 
    172, 197,  39,  55, 172, 197, 
     39,  55,   0,   0,   0,   0, 
      2,  64,   0,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      0, 137,  65,  62,   0,   0, 
      0,   0,  50,   0,   0,  15, 
    194,   0,  16,   0,   3,   0, 
      0,   0,   6,   4,  16,   0, 
      3,   0,   0,   0,   2,  64, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   0,   0, 172, 197, 
     39,  55, 172, 197,  39,  55, 
      2,  64,   0,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
    205, 111, 245,  70, 205, 111, 
    245,  70,  16,   0,   0,  10, 
    130,   0,  16,   0,   1,   0, 
      0,   0,   2,  64,   0,   0, 
    130,  43,  85,  65, 240,  22, 
    188,  65, 153, 176, 173,  65, 
      0,   0,   0,   0,  70,   2, 
     16,   0,   4,   0,   0,   0, 
     16,   0,   0,  10, 130,   0, 
     16,   0,   2,   0,   0,   0, 
      2,  64,   0,   0,  56, 248, 
    168,  65, 127, 217, 229,  65, 
     50, 230,  62,  65,   0,   0, 
      0,   0,  70,   2,  16,   0, 
      4,   0,   0,   0,  26,   0, 
      0,   5,  18,   0,  16,   0, 
      4,   0,   0,   0,  58,   0, 
     16,   0,   1,   0,   0,   0, 
     26,   0,   0,   5,  34,   0, 
     16,   0,   4,   0,   0,   0, 
     58,   0,  16,   0,   2,   0, 
      0,   0,  14,   0,   0,   7, 
    194,   0,  16,   0,   3,   0, 
      0,   0, 166,  14,  16,   0, 
      3,   0,   0,   0,   6,   4, 
     16,   0,   4,   0,   0,   0, 
     26,   0,   0,   5, 194,   0, 
     16,   0,   3,   0,   0,   0, 
    166,  14,  16,   0,   3,   0, 
      0,   0,   0,   0,   0,   8, 
     50,   0,  16,   0,   3,   0, 
      0,   0,  22,   5,  16,   0, 
      2,   0,   0,   0,  70,   0, 
     16, 128,  65,   0,   0,   0, 
      3,   0,   0,   0,   0,   0, 
      0,  10,  50,   0,  16,   0, 
      3,   0,   0,   0,  70,   0, 
     16,   0,   3,   0,   0,   0, 
      2,  64,   0,   0,   0,   0, 
      0, 191,   0,   0,   0, 191, 
      0,   0,   0,   0,   0,   0, 
      0,   0,  50,   0,   0,  12, 
     50,   0,  16,   0,   3,   0, 
      0,   0, 230,  10,  16,   0, 
      3,   0,   0,   0,   2,  64, 
      0,   0, 102, 102, 102,  63, 
    102, 102, 102,  63,   0,   0, 
      0,   0,   0,   0,   0,   0, 
     70,   0,  16,   0,   3,   0, 
      0,   0,   0,   0,   0,  10, 
     50,   0,  16,   0,   3,   0, 
      0,   0,  70,   0,  16,   0, 
      3,   0,   0,   0,   2,  64, 
      0,   0, 102, 102, 230, 190, 
    102, 102, 230, 190,   0,   0, 
      0,   0,   0,   0,   0,   0, 
     56,   0,   0,  10,  50,   0, 
     16,   0,   2,   0,   0,   0, 
     70,   0,  16,   0,   2,   0, 
      0,   0,   2,  64,   0,   0, 
      0,   0,  32,  65,   0,   0, 
     32,  65,   0,   0,   0,   0, 
      0,   0,   0,   0,  26,   0, 
      0,   5,  50,   0,  16,   0, 
      2,   0,   0,   0,  70,   0, 
     16,   0,   2,   0,   0,   0, 
     50,   0,   0,  15,  50,   0, 
     16,   0,   2,   0,   0,   0, 
     70,   0,  16,   0,   2,   0, 
      0,   0,   2,  64,   0,   0, 
      0,   0,   0,  64,   0,   0, 
      0,  64,   0,   0,   0,   0, 
      0,   0,   0,   0,   2,  64, 
      0,   0,   0,   0, 128, 191, 
      0,   0, 128, 191,   0,   0, 
      0,   0,   0,   0,   0,   0, 
     50,   0,   0,  14,  50,   0, 
     16,   0,   2,   0,   0,   0, 
     70,   0,  16, 128, 129,   0, 
      0,   0,   2,   0,   0,   0, 
      2,  64,   0,   0,  10, 215, 
     35,  60,  10, 215,  35,  60, 
      0,   0,   0,   0,   0,   0, 
      0,   0,  70,   0,  16, 128, 
    129,   0,   0,   0,   3,   0, 
      0,   0,   0,   0,   0,   8, 
    130,   0,  16,   0,   1,   0, 
      0,   0,  26,   0,  16, 128, 
     65,   0,   0,   0,   2,   0, 
      0,   0,  10,   0,  16,   0, 
      2,   0,   0,   0,   0,   0, 
      0,   7, 130,   0,  16,   0, 
      2,   0,   0,   0,  26,   0, 
     16,   0,   2,   0,   0,   0, 
     10,   0,  16,   0,   2,   0, 
      0,   0,  52,   0,   0,   7, 
    130,   0,  16,   0,   1,   0, 
      0,   0,  58,   0,  16,   0, 
      1,   0,   0,   0,  58,   0, 
     16,   0,   2,   0,   0,   0, 
     52,   0,   0,   7,  18,   0, 
     16,   0,   2,   0,   0,   0, 
     26,   0,  16,   0,   2,   0, 
      0,   0,  10,   0,  16,   0, 
      2,   0,   0,   0,  50,   0, 
      0,   9, 130,   0,  16,   0, 
      1,   0,   0,   0,  58,   0, 
     16,   0,   1,   0,   0,   0, 
      1,  64,   0,   0, 154, 153, 
     25,  63,  10,   0,  16,   0, 
      2,   0,   0,   0,  50,   0, 
      0,  10,  66,   0,  16,   0, 
      1,   0,   0,   0,  10,   0, 
     16, 128,  65,   0,   0,   0, 
      0,   0,   0,   0,   1,  64, 
      0,   0,   0,   0, 160,  64, 
     42,   0,  16,   0,   1,   0, 
      0,   0,   0,   0,   0,  10, 
    194,   0,  16,   0,   1,   0, 
      0,   0, 166,  14,  16,   0, 
      1,   0,   0,   0,   2,  64, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
    160, 192,  10, 215,  35, 188, 
     56,   0,   0,   8,  66,   0, 
     16,   0,   1,   0,   0,   0, 
     42,   0,  16, 128, 129,   0, 
      0,   0,   1,   0,   0,   0, 
      1,  64,   0,   0,   0,   0, 
      0,  63,  51,   0,   0,   7, 
     66,   0,  16,   0,   1,   0, 
      0,   0,  42,   0,  16,   0, 
      1,   0,   0,   0,   1,  64, 
      0,   0,   0,   0, 128,  63, 
     50,   0,   0,   9,  66,   0, 
     16,   0,   1,   0,   0,   0, 
     42,   0,  16,   0,   1,   0, 
      0,   0,   1,  64,   0,   0, 
    205, 204,  76,  61,   1,  64, 
      0,   0, 205, 204,  76,  61, 
     56,   0,   0,   7,  18,   0, 
     16,   0,   2,   0,   0,   0, 
     42,   0,  16,   0,   1,   0, 
      0,   0,   1,  64,   0,   0, 
      0,   0,   0, 192,   0,   0, 
      0,   8,  66,   0,  16,   0, 
      1,   0,   0,   0,  42,   0, 
     16, 128,  65,   0,   0,   0, 
      1,   0,   0,   0,  58,   0, 
     16,   0,   1,   0,   0,   0, 
     14,   0,   0,  10, 130,   0, 
     16,   0,   1,   0,   0,   0, 
      2,  64,   0,   0,   0,   0, 
    128,  63,   0,   0, 128,  63, 
      0,   0, 128,  63,   0,   0, 
    128,  63,  10,   0,  16,   0, 
      2,   0,   0,   0,  56,  32, 
      0,   7,  66,   0,  16,   0, 
      1,   0,   0,   0,  58,   0, 
     16,   0,   1,   0,   0,   0, 
     42,   0,  16,   0,   1,   0, 
      0,   0,  50,   0,   0,   9, 
    130,   0,  16,   0,   1,   0, 
      0,   0,  42,   0,  16,   0, 
      1,   0,   0,   0,   1,  64, 
      0,   0,   0,   0,   0, 192, 
      1,  64,   0,   0,   0,   0, 
     64,  64,  56,   0,   0,   7, 
     66,   0,  16,   0,   1,   0, 
      0,   0,  42,   0,  16,   0, 
      1,   0,   0,   0,  42,   0, 
     16,   0,   1,   0,   0,   0, 
     56,   0,   0,   7,  66,   0, 
     16,   0,   1,   0,   0,   0, 
     42,   0,  16,   0,   1,   0, 
      0,   0,  58,   0,  16,   0, 
      1,   0,   0,   0,  14,   0, 
      0,   7, 130,   0,  16,   0, 
      1,   0,   0,   0,  42,   0, 
     16,   0,   3,   0,   0,   0, 
     42,   0,  16,   0,   2,   0, 
      0,   0,  50,   0,   0,   9, 
     18,   0,  16,   0,   1,   0, 
      0,   0,  42,   0,  16,   0, 
      1,   0,   0,   0,  58,   0, 
     16,   0,   1,   0,   0,   0, 
     10,   0,  16,   0,   1,   0, 
      0,   0,  30,   0,   0,   7, 
     34,   0,  16,   0,   1,   0, 
      0,   0,  26,   0,  16,   0, 
      1,   0,   0,   0,   1,  64, 
      0,   0,   1,   0,   0,   0, 
     22,   0,   0,   1,  54,   0, 
      0,   5, 130,  32,  16,   0, 
      0,   0,   0,   0,  10,   0, 
     16,   0,   1,   0,   0,   0, 
     54,   0,   0,   8, 114,  32, 
     16,   0,   0,   0,   0,   0, 
      2,  64,   0,   0,   0,   0, 
    128,  63,   0,   0, 128,  63, 
      0,   0, 128,  63,   0,   0, 
      0,   0,  62,   0,   0,   1, 
     83,  84,  65,  84, 116,   0, 
      0,   0,  62,   0,   0,   0, 
      5,   0,   0,   0,   0,   0, 
      0,   0,   2,   0,   0,   0, 
     52,   0,   0,   0,   2,   0, 
      0,   0,   0,   0,   0,   0, 
      1,   0,   0,   0,   1,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      3,   0,   0,   0,   0,   0, 
      0,   0,   8,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   0,   0
};

const BYTE g_VertyShader[] = {
	68, 88, 66, 67, 76, 75,
	108, 163, 29, 221, 215, 151,
	233, 28, 62, 114, 145, 31,
	52, 111, 1, 0, 0, 0,
	176, 2, 0, 0, 5, 0,
	0, 0, 52, 0, 0, 0,
	128, 0, 0, 0, 180, 0,
	0, 0, 12, 1, 0, 0,
	52, 2, 0, 0, 82, 68,
	69, 70, 68, 0, 0, 0,
	0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0,
	28, 0, 0, 0, 0, 4,
	254, 255, 0, 1, 0, 0,
	28, 0, 0, 0, 77, 105,
	99, 114, 111, 115, 111, 102,
	116, 32, 40, 82, 41, 32,
	72, 76, 83, 76, 32, 83,
	104, 97, 100, 101, 114, 32,
	67, 111, 109, 112, 105, 108,
	101, 114, 32, 49, 48, 46,
	49, 0, 73, 83, 71, 78,
	44, 0, 0, 0, 1, 0,
	0, 0, 8, 0, 0, 0,
	32, 0, 0, 0, 0, 0,
	0, 0, 6, 0, 0, 0,
	1, 0, 0, 0, 0, 0,
	0, 0, 1, 1, 0, 0,
	83, 86, 95, 86, 69, 82,
	84, 69, 88, 73, 68, 0,
	79, 83, 71, 78, 80, 0,
	0, 0, 2, 0, 0, 0,
	8, 0, 0, 0, 56, 0,
	0, 0, 0, 0, 0, 0,
	1, 0, 0, 0, 3, 0,
	0, 0, 0, 0, 0, 0,
	15, 0, 0, 0, 68, 0,
	0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 3, 0,
	0, 0, 1, 0, 0, 0,
	3, 12, 0, 0, 83, 86,
	95, 80, 79, 83, 73, 84,
	73, 79, 78, 0, 84, 69,
	88, 67, 79, 79, 82, 68,
	0, 171, 171, 171, 83, 72,
	68, 82, 32, 1, 0, 0,
	64, 0, 1, 0, 72, 0,
	0, 0, 96, 0, 0, 4,
	18, 16, 16, 0, 0, 0,
	0, 0, 6, 0, 0, 0,
	103, 0, 0, 4, 242, 32,
	16, 0, 0, 0, 0, 0,
	1, 0, 0, 0, 101, 0,
	0, 3, 50, 32, 16, 0,
	1, 0, 0, 0, 104, 0,
	0, 2, 1, 0, 0, 0,
	54, 0, 0, 8, 194, 32,
	16, 0, 0, 0, 0, 0,
	2, 64, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0,
	128, 63, 1, 0, 0, 7,
	18, 0, 16, 0, 0, 0,
	0, 0, 10, 16, 16, 0,
	0, 0, 0, 0, 1, 64,
	0, 0, 1, 0, 0, 0,
	85, 0, 0, 7, 66, 0,
	16, 0, 0, 0, 0, 0,
	10, 16, 16, 0, 0, 0,
	0, 0, 1, 64, 0, 0,
	1, 0, 0, 0, 86, 0,
	0, 5, 50, 0, 16, 0,
	0, 0, 0, 0, 134, 0,
	16, 0, 0, 0, 0, 0,
	0, 0, 0, 10, 194, 0,
	16, 0, 0, 0, 0, 0,
	6, 4, 16, 0, 0, 0,
	0, 0, 2, 64, 0, 0,
	0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 191,
	0, 0, 0, 191, 54, 0,
	0, 5, 50, 32, 16, 0,
	1, 0, 0, 0, 70, 0,
	16, 0, 0, 0, 0, 0,
	0, 0, 0, 7, 18, 32,
	16, 0, 0, 0, 0, 0,
	42, 0, 16, 0, 0, 0,
	0, 0, 42, 0, 16, 0,
	0, 0, 0, 0, 56, 0,
	0, 7, 34, 32, 16, 0,
	0, 0, 0, 0, 58, 0,
	16, 0, 0, 0, 0, 0,
	1, 64, 0, 0, 0, 0,
	0, 192, 62, 0, 0, 1,
	83, 84, 65, 84, 116, 0,
	0, 0, 9, 0, 0, 0,
	1, 0, 0, 0, 0, 0,
	0, 0, 3, 0, 0, 0,
	3, 0, 0, 0, 0, 0,
	0, 0, 2, 0, 0, 0,
	1, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0,
	2, 0, 0, 0, 0, 0,
	0, 0, 1, 0, 0, 0,
	0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0,
	0, 0, 0, 0
};

#include <dwmapi.h>
#include <mmsystem.h>
#include <setupapi.h>

static const GUID GUID_DEVINTERFACE_DISPLAY_ADAPTER = { 0x5b45201d, 0xf2f2, 0x4f3b, { 0x85, 0xbb, 0x30, 0xff, 0x1f, 0x95, 0x35, 0x99 } };

#pragma comment(lib, "dwmapi")

static bool IsSafeGPUDriver()
{
	static auto hSetupAPI = LoadLibraryW(L"setupapi.dll");
	if (!hSetupAPI)
	{
		return false;
	}

	static auto _SetupDiGetClassDevsW = (decltype(&SetupDiGetClassDevsW))GetProcAddress(hSetupAPI, "SetupDiGetClassDevsW");
	static auto _SetupDiBuildDriverInfoList = (decltype(&SetupDiBuildDriverInfoList))GetProcAddress(hSetupAPI, "SetupDiBuildDriverInfoList");
	static auto _SetupDiEnumDeviceInfo = (decltype(&SetupDiEnumDeviceInfo))GetProcAddress(hSetupAPI, "SetupDiEnumDeviceInfo");
	static auto _SetupDiEnumDriverInfoW = (decltype(&SetupDiEnumDriverInfoW))GetProcAddress(hSetupAPI, "SetupDiEnumDriverInfoW");
	static auto _SetupDiDestroyDeviceInfoList = (decltype(&SetupDiDestroyDeviceInfoList))GetProcAddress(hSetupAPI, "SetupDiDestroyDeviceInfoList");

	HDEVINFO devInfoSet = _SetupDiGetClassDevsW(&GUID_DEVINTERFACE_DISPLAY_ADAPTER, NULL, NULL,
	DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

	bool safe = true;

	for (int i = 0;; i++)
	{
		SP_DEVINFO_DATA devInfo = { sizeof(SP_DEVINFO_DATA) };
		if (!_SetupDiEnumDeviceInfo(devInfoSet, i, &devInfo))
		{
			break;
		}

		if (!_SetupDiBuildDriverInfoList(devInfoSet, &devInfo, SPDIT_COMPATDRIVER))
		{
			safe = false;
			break;
		}

		SP_DRVINFO_DATA drvInfo = { sizeof(SP_DRVINFO_DATA) };
		if (_SetupDiEnumDriverInfoW(devInfoSet, &devInfo, SPDIT_COMPATDRIVER, 0, &drvInfo))
		{
			ULARGE_INTEGER driverDate = {0};
			driverDate.HighPart = drvInfo.DriverDate.dwHighDateTime;
			driverDate.LowPart = drvInfo.DriverDate.dwLowDateTime;
			
			// drivers from after 2007-01-01 (to prevent in-box driver from being wrong) and 2020-01-01 are 'unsafe' and might crash
			if (driverDate.QuadPart >= 128120832000000000ULL && driverDate.QuadPart < 132223104000000000ULL)
			{
				safe = false;
				break;
			}
		}
	}

	_SetupDiDestroyDeviceInfoList(devInfoSet);

	return safe;
}

static void InitializeRenderOverlay(winrt::Windows::UI::Xaml::Controls::SwapChainPanel swapChainPanel, int w, int h)
{
	auto nativePanel = swapChainPanel.as<ISwapChainPanelNative>();

	auto run = [w, h, swapChainPanel, nativePanel]()
	{
		auto loadSystemDll = [](auto dll)
		{
			wchar_t systemPath[512];
			GetSystemDirectory(systemPath, _countof(systemPath));

			wcscat_s(systemPath, dll);

			return LoadLibrary(systemPath);
		};

		ComPtr<ID3D11Device> g_pd3dDevice = NULL;
		ComPtr<ID3D11DeviceContext> g_pd3dDeviceContext = NULL;
		ComPtr<IDXGISwapChain1> g_pSwapChain = NULL;
		ComPtr<ID3D11RenderTargetView> g_mainRenderTargetView = NULL;

		// Setup swap chain
		DXGI_SWAP_CHAIN_DESC1 sd;
		ZeroMemory(&sd, sizeof(sd));
		sd.BufferCount = 2;
		sd.Width = w;
		sd.Height = h;
		sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.Scaling = DXGI_SCALING_STRETCH;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

		UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		//createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
		D3D_FEATURE_LEVEL featureLevel;

		// so we'll fail if FL11 isn't supported
		const D3D_FEATURE_LEVEL featureLevelArray[1] = {
			D3D_FEATURE_LEVEL_11_0,
		};

		auto d3d11 = loadSystemDll(L"\\d3d11.dll");
		auto _D3D11CreateDevice = (decltype(&D3D11CreateDevice))GetProcAddress(d3d11, "D3D11CreateDevice");

		if (_D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, std::size(featureLevelArray), D3D11_SDK_VERSION, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
		{
			return;
		}

		ComPtr<IDXGIDevice1> device1;
		if (FAILED(g_pd3dDevice.As(&device1)))
		{
			return;
		}

		ComPtr<IDXGIAdapter> adapter;
		if (FAILED(device1->GetAdapter(&adapter)))
		{
			return;
		}

		ComPtr<IDXGIFactory> parent;
		if (FAILED(adapter->GetParent(__uuidof(IDXGIFactory), &parent)))
		{
			return;
		}

		ComPtr<IDXGIFactory3> factory3;
		if (FAILED(parent.As(&factory3)))
		{
			return;
		}

		if (FAILED(factory3->CreateSwapChainForComposition(g_pd3dDevice.Get(), &sd, NULL, &g_pSwapChain)))
		{
			return;
		}

		{
			ComPtr<ID3D11Texture2D> pBackBuffer;
			g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
			g_pd3dDevice->CreateRenderTargetView(pBackBuffer.Get(), NULL, &g_mainRenderTargetView);
		}

		swapChainPanel.Dispatcher().TryRunAsync(
		winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
		[g_pSwapChain, nativePanel]()
		{
			nativePanel->SetSwapChain(g_pSwapChain.Get());
		});

		ComPtr<ID3D11VertexShader> vs;
		ComPtr<ID3D11PixelShader> ps;

		g_pd3dDevice->CreatePixelShader(g_PixyShader, sizeof(g_PixyShader), NULL, &ps);
		g_pd3dDevice->CreateVertexShader(g_VertyShader, sizeof(g_VertyShader), NULL, &vs);

		ComPtr<ID3D11BlendState> bs;

		{
			D3D11_BLEND_DESC desc = { 0 };
			desc.AlphaToCoverageEnable = false;
			desc.RenderTarget[0].BlendEnable = true;
			desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
			desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

			g_pd3dDevice->CreateBlendState(&desc, &bs);
		}

		struct CBuf
		{
			float res[2];
			float sec;
			float pad;
		};

		ComPtr<ID3D11Buffer> cbuf;

		{
			D3D11_BUFFER_DESC desc;
			desc.ByteWidth = sizeof(CBuf);
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.MiscFlags = 0;
			g_pd3dDevice->CreateBuffer(&desc, NULL, &cbuf);
		}

		while (g_uui.ten)
		{
			// Setup viewport
			D3D11_VIEWPORT vp;
			memset(&vp, 0, sizeof(D3D11_VIEWPORT));
			vp.Width = w;
			vp.Height = h;
			vp.MinDepth = 0.0f;
			vp.MaxDepth = 1.0f;
			vp.TopLeftX = vp.TopLeftY = 0;
			g_pd3dDeviceContext->RSSetViewports(1, &vp);

			auto rtv = g_mainRenderTargetView.Get();
			g_pd3dDeviceContext->OMSetRenderTargets(1, &rtv, NULL);

			float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			g_pd3dDeviceContext->ClearRenderTargetView(rtv, clearColor);

			g_pd3dDeviceContext->VSSetShader(vs.Get(), NULL, 0);
			g_pd3dDeviceContext->PSSetShader(ps.Get(), NULL, 0);

			auto cb = cbuf.Get();
			g_pd3dDeviceContext->VSSetConstantBuffers(0, 1, &cb);
			g_pd3dDeviceContext->PSSetConstantBuffers(0, 1, &cb);

			g_pd3dDeviceContext->OMSetBlendState(bs.Get(), NULL, 0xFFFFFFFF);

			static auto startTime = std::chrono::steady_clock::now();

			D3D11_MAPPED_SUBRESOURCE mapped_resource;
			if (SUCCEEDED(g_pd3dDeviceContext->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource)))
			{
				auto c = static_cast<CBuf*>(mapped_resource.pData);
				c->res[0] = static_cast<float>(w);
				c->res[1] = static_cast<float>(h);
				auto now = std::chrono::steady_clock::now();
				c->sec = std::chrono::duration<float>(now - startTime).count();
				g_pd3dDeviceContext->Unmap(cb, 0);
			}

			g_pd3dDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
			g_pd3dDeviceContext->Draw(4, 0);

			g_pSwapChain->Present(0, 0);
			DwmFlush();
		}
	};

	std::thread([run]()
	{
		if (IsSafeGPUDriver())
		{
			run();
		}

		// prevent the thread from exiting (the CRT is broken and will crash on thread exit in some cases)
		WaitForSingleObject(GetCurrentProcess(), INFINITE);
	}).detach();
}

// Christmas Effects - Snow and String Lights (Dec/Jan only)
static void InitializeChristmasEffects(winrt::Windows::UI::Xaml::FrameworkElement const& ui, int width, int height)
{
	try
	{
		// Find and show Christmas lights canvas - also use it for snow
		auto lightsObj = ui.FindName(L"ChristmasLights");
		if (!lightsObj) return;
		
		auto lightsCanvas = lightsObj.as<winrt::Windows::UI::Xaml::Controls::Canvas>();
		lightsCanvas.Visibility(winrt::Windows::UI::Xaml::Visibility::Visible);
		
		// Get compositor for effects
		auto compositor = winrt::Windows::UI::Xaml::Window::Current().Compositor();
		
		// Create snow dots (Ellipse) - add to the same canvas as lights
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_real_distribution<float> xDist(50.0f, static_cast<float>(width) - 50.0f);
		std::uniform_real_distribution<float> yStartDist(-200.0f, -20.0f); // Random start heights
		std::uniform_real_distribution<float> sizeDist(3.0f, 6.0f);
		std::uniform_real_distribution<float> durationDist(4.0f, 9.0f); // Varied speeds
		std::uniform_real_distribution<float> delayDist(0.0f, 8.0f); // Stagger starts
		
		const int snowflakeCount = 100; // More dots for continuous effect
		
		for (int i = 0; i < snowflakeCount; i++)
		{
			float size = sizeDist(gen);
			
			// Create Ellipse (white dot)
			auto snowDot = winrt::Windows::UI::Xaml::Shapes::Ellipse();
			snowDot.Width(size);
			snowDot.Height(size);
			snowDot.Fill(winrt::Windows::UI::Xaml::Media::SolidColorBrush(
				winrt::Windows::UI::Colors::White()));
			snowDot.Opacity(0.7 + (gen() % 30) * 0.01);
			
			float startX = xDist(gen);
			float duration = durationDist(gen);
			float delay = delayDist(gen);
			
			// Make some dots start mid-screen to create immediate falling effect
			float startY;
			if (i % 3 == 0) {
				// Start from current position in the fall cycle (already falling)
				std::uniform_real_distribution<float> midScreenDist(-50.0f, static_cast<float>(height) * 0.6f);
				startY = midScreenDist(gen);
				delay = 0; // No delay for already-falling dots
			} else {
				startY = yStartDist(gen); // Start from top
			}
			
			// Position
			winrt::Windows::UI::Xaml::Controls::Canvas::SetLeft(snowDot, startX);
			winrt::Windows::UI::Xaml::Controls::Canvas::SetTop(snowDot, startY);
			
			// Add to lights canvas
			lightsCanvas.Children().Append(snowDot);
			
			// Add glow to snow (using SpriteVisual + SetElementChildVisual)
			auto sprite = compositor.CreateSpriteVisual();
			sprite.Size({ size, size });
			sprite.Brush(compositor.CreateColorBrush(winrt::Windows::UI::Colors::White()));
			
			// Clip sprite to circle geometry so shadow is also circular
			auto geometry = compositor.CreateEllipseGeometry();
			geometry.Radius({ size / 2.0f, size / 2.0f });
			geometry.Center({ size / 2.0f, size / 2.0f });
			auto clip = compositor.CreateGeometricClip(geometry);
			sprite.Clip(clip);
			
			auto shadow = compositor.CreateDropShadow();
			shadow.BlurRadius(8.0f);
			shadow.Color(winrt::Windows::UI::Colors::White());
			shadow.Opacity(0.9f);
			sprite.Shadow(shadow);
			
			winrt::Windows::UI::Xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(snowDot, sprite);
			
			// Create DoubleAnimation for Top property
			auto storyboard = winrt::Windows::UI::Xaml::Media::Animation::Storyboard();
			auto animation = winrt::Windows::UI::Xaml::Media::Animation::DoubleAnimation();
			
			animation.From(startY);
			animation.To(static_cast<double>(height) + 50.0);
			animation.Duration(winrt::Windows::UI::Xaml::DurationHelper::FromTimeSpan(
				std::chrono::milliseconds(static_cast<int>(duration * 1000))));
			animation.BeginTime(winrt::Windows::Foundation::TimeSpan(
				std::chrono::milliseconds(static_cast<int>(delay * 1000))));
			animation.RepeatBehavior(winrt::Windows::UI::Xaml::Media::Animation::RepeatBehaviorHelper::Forever());
			
			storyboard.Children().Append(animation);
			winrt::Windows::UI::Xaml::Media::Animation::Storyboard::SetTarget(animation, snowDot);
			winrt::Windows::UI::Xaml::Media::Animation::Storyboard::SetTargetProperty(animation, L"(Canvas.Top)");
			
			storyboard.Begin();
		}
		
		// Add blinking animation to bulbs
		for (int i = 1; i <= 14; i++)
		{
			try
			{
				auto bulbName = L"bulb" + std::to_wstring(i);
				auto bulb = ui.FindName(winrt::hstring(bulbName)).as<winrt::Windows::UI::Xaml::Shapes::Ellipse>();
				
				// Add colored glow to bulb - use container visual approach
				auto fillBrush = bulb.Fill().as<winrt::Windows::UI::Xaml::Media::SolidColorBrush>();
				auto bulbColor = fillBrush.Color();
				
				auto dropShadow = compositor.CreateDropShadow();
				dropShadow.BlurRadius(15.0f);
				dropShadow.Color(bulbColor);
				dropShadow.Opacity(0.95f);
				dropShadow.Offset({ 0, 0, 0 });
				
				// Create sprite visual with shadow for glow
				auto sprite = compositor.CreateSpriteVisual();
				sprite.Size({ 14.0f, 20.0f });
				sprite.Brush(compositor.CreateColorBrush(bulbColor));
				
				// Clip sprite to ellipse geometry
				auto geometry = compositor.CreateEllipseGeometry();
				geometry.Radius({ 7.0f, 10.0f });
				geometry.Center({ 7.0f, 10.0f });
				auto clip = compositor.CreateGeometricClip(geometry);
				sprite.Clip(clip);
				
				sprite.Shadow(dropShadow);
				
				// Set as child visual
				winrt::Windows::UI::Xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(bulb, sprite);
				
				// Animate opacity on the element visual
				auto visual = winrt::Windows::UI::Xaml::Hosting::ElementCompositionPreview::GetElementVisual(bulb);
				
				auto opacityAnimation = compositor.CreateScalarKeyFrameAnimation();
				opacityAnimation.InsertKeyFrame(0.0f, 1.0f);
				opacityAnimation.InsertKeyFrame(0.5f, 0.3f);
				opacityAnimation.InsertKeyFrame(1.0f, 1.0f);
				opacityAnimation.Duration(std::chrono::milliseconds(600 + (i * 80)));
				opacityAnimation.IterationBehavior(winrt::Windows::UI::Composition::AnimationIterationBehavior::Forever);
				
				visual.StartAnimation(L"Opacity", opacityAnimation);
			}
			catch (...) {}
		}
	}
	catch (...)
	{
		// Silently fail if Christmas effects cannot be initialized
	}
}

void UI_CreateWindow()
{
	g_uui.taskbarMsg = RegisterWindowMessage(L"TaskbarButtonCreated");

	HWND rootWindow = CreateWindowEx(0, L"NotSteamAtAll", PRODUCT_NAME, 13238272 /* lol */, 0x80000000, 0, g_dpi.ScaleX(500), g_dpi.ScaleY(129), NULL, NULL, GetModuleHandle(NULL), 0);

	int wwidth = 500;
	int wheight = 139;

	if (!g_uui.tenMode)
	{
		INITCOMMONCONTROLSEX controlSex;
		controlSex.dwSize = sizeof(controlSex);
		controlSex.dwICC = 16416; // lazy bum
		InitCommonControlsEx(&controlSex);

		HFONT font = UI_CreateScaledFont(-12, 0, 0, 0, 0, 0, 0, 0, 1, 8, 0, 5, 2, L"Tahoma");

		// TODO: figure out which static is placed where
		HWND static1 = CreateWindowEx(0x20, L"static", L"static1", 0x50000000, g_dpi.ScaleX(15), g_dpi.ScaleY(15), g_dpi.ScaleX(455), g_dpi.ScaleY(25), rootWindow, 0, GetModuleHandle(NULL) /* what?! */, 0);

		SendMessage(static1, WM_SETFONT, (WPARAM)font, 0);

		HWND cancelButton = CreateWindowEx(0, L"button", L"Cancel", 0x50000000, g_dpi.ScaleX(395), g_dpi.ScaleY(64), g_dpi.ScaleX(75), g_dpi.ScaleY(25), rootWindow, 0, GetModuleHandle(NULL), 0);
		SendMessage(cancelButton, WM_SETFONT, (WPARAM)font, 0);

		HWND progressBar = CreateWindowEx(0, L"msctls_progress32", 0, 0x50000000, g_dpi.ScaleX(15), g_dpi.ScaleY(40), g_dpi.ScaleX(455), g_dpi.ScaleY(15), rootWindow, 0, GetModuleHandle(NULL), 0);
		SendMessage(progressBar, PBM_SETRANGE32, 0, 10000);

		HWND static2 = CreateWindowEx(0x20, L"static", L"static2", 0x50000000, g_dpi.ScaleX(15), g_dpi.ScaleY(68), g_dpi.ScaleX(370), g_dpi.ScaleY(25), rootWindow, 0, GetModuleHandle(NULL) /* what?! */, 0);
		SendMessage(static2, WM_SETFONT, (WPARAM)font, 0);

		g_uui.cancelButton = cancelButton;
		g_uui.progressBar = progressBar;
		g_uui.topStatic = static1;
		g_uui.bottomStatic = static2;
	}
	else
	{
		wwidth = 854;
		wheight = 480;

		// make TenUI
		auto ten = std::make_unique<TenUI>();
		ten->uiSource = std::move(DesktopWindowXamlSource{});

		// attach window
		auto interop = ten->uiSource.as<IDesktopWindowXamlSourceNative>();
		winrt::check_hresult(interop->AttachToWindow(rootWindow));

		// setup position
		HWND childHwnd;
		interop->get_WindowHandle(&childHwnd);

		SetWindowLong(childHwnd, GWL_EXSTYLE, GetWindowLong(childHwnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT | WS_EX_LAYERED);

		SetWindowPos(childHwnd, 0, 0, 0, g_dpi.ScaleX(wwidth), g_dpi.ScaleY(wheight), SWP_SHOWWINDOW);

		// Apply rounded corners to window (Windows 11+)
		{
			typedef HRESULT(WINAPI* DwmSetWindowAttributeFunc)(HWND, DWORD, LPCVOID, DWORD);
			auto dwmapi = LoadLibraryW(L"dwmapi.dll");
			if (dwmapi)
			{
				auto pDwmSetWindowAttribute = (DwmSetWindowAttributeFunc)GetProcAddress(dwmapi, "DwmSetWindowAttribute");
				if (pDwmSetWindowAttribute)
				{
					// DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2
					DWORD cornerPreference = 2;
					pDwmSetWindowAttribute(rootWindow, 33, &cornerPreference, sizeof(cornerPreference));
				}
			}
		}

		auto doc = winrt::Windows::UI::Xaml::Markup::XamlReader::Load(g_mainXaml);
		auto ui = doc.as<winrt::Windows::UI::Xaml::FrameworkElement>();

		auto bg = ui.FindName(L"BackdropGrid").as<winrt::Windows::UI::Xaml::Controls::Grid>();
		bg.Background(winrt::make<BackdropBrush>());

		// Store backdrop grid reference for parallax
		ten->backdropGrid = bg;

		// Add pointer move event for parallax effect
		ui.PointerMoved([wwidth, wheight](winrt::Windows::Foundation::IInspectable const& sender, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& e)
		{
			if (!g_uui.ten || !g_uui.ten->effectBrush)
				return;

			try
			{
				winrt::Windows::UI::Input::PointerPoint point = e.GetCurrentPoint(sender.as<winrt::Windows::UI::Xaml::UIElement>());
				winrt::Windows::Foundation::Point pos = point.Position();

				// Calculate offset from center (normalized -1 to 1)
				float centerX = static_cast<float>(wwidth) / 2.0f;
				float centerY = static_cast<float>(wheight) / 2.0f;
				float deltaX = (pos.X - centerX) / centerX;
				float deltaY = (pos.Y - centerY) / centerY;

				// Apply parallax offset (max 30 pixels)
				float offsetX = deltaX * -30.0f;
				float offsetY = deltaY * -30.0f;

				// Create new transform with offset
				winrt::Windows::Foundation::Numerics::float3x2 newTransform = g_uui.ten->baseTransform;
				newTransform.m31 += offsetX;
				newTransform.m32 += offsetY;

				// Update effect brush transform
				g_uui.ten->effectBrush.Properties().InsertMatrix3x2(L"xform.TransformMatrix", newTransform);
			}
			catch (...) {}
		});

		// Snow effect removed - overlay panel kept for potential future use
		// auto sc = ui.FindName(L"Overlay").as<winrt::Windows::UI::Xaml::Controls::SwapChainPanel>();

		// Initialize Christmas effects (December and January only)
		{
			auto time = std::time(nullptr);
			auto datetime = std::localtime(&time);
			auto month = datetime->tm_mon + 1;

			if (month == 12 || month == 1)
			{
				InitializeChristmasEffects(ui, wwidth, wheight);
			}
		}
		
		/*auto shadow = ui.FindName(L"SharedShadow").as<winrt::Windows::UI::Xaml::Media::ThemeShadow>();
		shadow.Receivers().Append(bg);*/

		ten->topStatic = ui.FindName(L"static1").as<winrt::Windows::UI::Xaml::Controls::TextBlock>();
		ten->bottomStatic = ui.FindName(L"static2").as<winrt::Windows::UI::Xaml::Controls::TextBlock>();
		ten->progressBar = ui.FindName(L"progressBar").as<winrt::Windows::UI::Xaml::Controls::Border>();
		ten->snailContainer = ui.FindName(L"snailContainer").as<winrt::Windows::UI::Xaml::UIElement>();

		ten->uiSource.Content(ui);

		g_uui.tenWindow = FindWindowExW(rootWindow, NULL, L"Windows.UI.Core.CoreWindow", NULL);

		g_uui.ten = std::move(ten);
	}

	g_uui.rootWindow = rootWindow;

	RECT wndRect;
	wndRect.left = 0;
	wndRect.top = 0;
	wndRect.right = g_dpi.ScaleX(wwidth);
	wndRect.bottom = g_dpi.ScaleY(wheight);

	HWND desktop = GetDesktopWindow();
	HDC dc = GetDC(desktop);
	int width = GetDeviceCaps(dc, 8);
	int height = GetDeviceCaps(dc, 10);

	ReleaseDC(desktop, dc);

	SetTimer(rootWindow, 0, 20, NULL);

	MoveWindow(rootWindow, (width - g_dpi.ScaleX(wwidth)) / 2, (height - g_dpi.ScaleY(wheight)) / 2, wndRect.right - wndRect.left, wndRect.bottom - wndRect.top, TRUE);

	ShowWindow(rootWindow, TRUE);
}

LRESULT CALLBACK UI_WndProc(HWND hWnd, UINT uMsg, WPARAM wparam, LPARAM lparam)
{
	switch (uMsg)
	{
		case WM_NCHITTEST:
			if (g_uui.tenMode)
			{
				return HTCAPTION;
			}
		case WM_NCCALCSIZE:
			if (g_uui.tenMode)
			{
				return 0;
			}
		case WM_NCCREATE:
			{
				// Only Windows 10+ supports EnableNonClientDpiScaling
				if (IsWindows10OrGreater())
				{
					HMODULE user32 = LoadLibrary(L"user32.dll");

					if (user32)
					{
						auto EnableNonClientDpiScaling = (decltype(&::EnableNonClientDpiScaling))GetProcAddress(user32, "EnableNonClientDpiScaling");

						if (EnableNonClientDpiScaling)
						{
							EnableNonClientDpiScaling(hWnd);
						}

						FreeLibrary(user32);
					}
				}

				return DefWindowProc(hWnd, uMsg, wparam, lparam);
			}
		
		case WM_CTLCOLORSTATIC:
			SetBkMode((HDC)wparam, TRANSPARENT);
			SetTextColor((HDC)wparam, COLORREF(GetSysColor(COLOR_WINDOWTEXT)));

			return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
		case WM_COMMAND:
			if ((HWND)lparam == g_uui.cancelButton)
			{
				g_uui.canceled = true;
			}

			break;
		case WM_TIMER:
			SetWindowText(g_uui.topStatic, g_uui.topText);
			SetWindowText(g_uui.bottomStatic, g_uui.bottomText);
			break;
		case WM_PAINT:
			{
				PAINTSTRUCT ps;
				HDC dc = BeginPaint(hWnd, &ps);
			
				EndPaint(hWnd, &ps);
				break;
			}
		case WM_DPICHANGED:
			{
				// Set the new DPI
				g_dpi.SetScale(LOWORD(wparam), HIWORD(wparam));

				// Resize the window
				LPRECT newScale = (LPRECT)lparam;
				SetWindowPos(hWnd, HWND_TOP, newScale->left, newScale->top, newScale->right - newScale->left, newScale->bottom - newScale->top, SWP_NOZORDER | SWP_NOACTIVATE);

				// Recreate the font
				HFONT newFont = UI_CreateScaledFont(-12, 0, 0, 0, 0, 0, 0, 0, 1, 8, 0, 5, 2, L"Tahoma");

				// Resize all components
				SetWindowPos(g_uui.topStatic, HWND_TOP, g_dpi.ScaleX(15), g_dpi.ScaleY(15), g_dpi.ScaleX(455), g_dpi.ScaleY(25), SWP_SHOWWINDOW);
				SendMessage(g_uui.topStatic, WM_SETFONT, (WPARAM)newFont, 0);

				SetWindowPos(g_uui.cancelButton, HWND_TOP, g_dpi.ScaleX(395), g_dpi.ScaleY(64), g_dpi.ScaleX(75), g_dpi.ScaleY(25), SWP_SHOWWINDOW);
				SendMessage(g_uui.cancelButton, WM_SETFONT, (WPARAM)newFont, 0);

				SetWindowPos(g_uui.progressBar, HWND_TOP, g_dpi.ScaleX(15), g_dpi.ScaleY(40), g_dpi.ScaleX(455), g_dpi.ScaleY(15), SWP_SHOWWINDOW);

				SetWindowPos(g_uui.bottomStatic, HWND_TOP, g_dpi.ScaleX(15), g_dpi.ScaleY(68), g_dpi.ScaleX(370), g_dpi.ScaleY(25), SWP_SHOWWINDOW);
				SendMessage(g_uui.bottomStatic, WM_SETFONT, (WPARAM)newFont, 0);
				break;
			}
		case WM_CLOSE:
			g_uui.canceled = true;
			return 0;
		default:
			if (uMsg == g_uui.taskbarMsg)
			{
				if (g_uui.tbList)
				{
					g_uui.tbList->SetProgressState(hWnd, TBPF_NORMAL);
					g_uui.tbList->SetProgressValue(hWnd, 0, 100);
				}
			}
			break;
	}

	return DefWindowProc(hWnd, uMsg, wparam, lparam);
}

void UI_RegisterClass()
{
	WNDCLASSEX wndClass = { 0 };
	wndClass.cbSize = sizeof(wndClass);
	wndClass.style = 3;
	wndClass.lpfnWndProc = UI_WndProc;
	wndClass.cbClsExtra = 0;
	wndClass.cbWndExtra = 0;
	wndClass.hInstance = GetModuleHandle(NULL);
	wndClass.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(1));
	wndClass.hCursor = LoadCursor(NULL, (LPCWSTR)0x7F02);
	wndClass.hbrBackground = (HBRUSH)6;
	wndClass.lpszClassName = L"NotSteamAtAll";
	wndClass.hIconSm = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(1));

	RegisterClassEx(&wndClass);
}

struct TenUIStorage;

static TenUIStorage* g_tenUI;

struct TenUIStorage : public TenUIBase
{
	// convoluted stuff to prevent WindowsXamlManager destruction weirdness
	static inline thread_local WindowsXamlManager* gManager{ nullptr };

	TenUIStorage()
	{
		g_tenUI = this;
	}

	void InitManager()
	{
		if (!gManager)
		{
			static thread_local WindowsXamlManager manager = WindowsXamlManager::InitializeForCurrentThread();
			gManager = &manager;
		}
	}

	virtual ~TenUIStorage() override
	{
		ShowWindow(g_uui.tenWindow, SW_HIDE);

		g_tenUI = nullptr;
	}

	static void ReallyBreakIt()
	{
		if (gManager)
		{
			gManager->Close();
		}
	}
};

std::unique_ptr<TenUIBase> UI_InitTen()
{
	// Windows 10 RS5+ gets a neat UI
	DWORDLONG viMask = 0;
	OSVERSIONINFOEXW osvi = { 0 };
	osvi.dwOSVersionInfoSize = sizeof(osvi);
	osvi.dwBuildNumber = 17763; // RS5+

	VER_SET_CONDITION(viMask, VER_BUILDNUMBER, VER_GREATER_EQUAL);

	bool forceOff = false;

	static HostSharedData<CfxState> initState("CfxInitState");

	if (initState->isReverseGame)
	{
		forceOff = true;
	}

	if (getenv("CitizenFX_NoTenUI"))
	{
		forceOff = true;
	}

#ifdef IS_LAUNCHER
	forceOff = true;
#endif

	if (VerifyVersionInfoW(&osvi, VER_BUILDNUMBER, viMask) && !forceOff)
	{
		RO_REGISTRATION_COOKIE cookie;

		g_uui.tenMode = true;

		try
		{
			return std::make_unique<TenUIStorage>();
		}
		catch (const std::exception&)
		{
		}
	}

	return std::make_unique<TenUIBase>();
}

void UI_DestroyTen()
{
	TenUIStorage::ReallyBreakIt();
}

void UI_DoCreation(bool safeMode)
{
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

	if (g_tenUI)
	{
		g_tenUI->InitManager();
	}

	if (IsWindows7OrGreater())
	{
		CoCreateInstance(CLSID_TaskbarList, 
			NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_uui.tbList));
	}

	// Only Windows 8.1+ supports per-monitor DPI awareness
	if (IsWindows8Point1OrGreater())
	{
		HMODULE shCore = LoadLibrary(L"shcore.dll");

		if (shCore)
		{
			auto GetDpiForMonitor = (decltype(&::GetDpiForMonitor))GetProcAddress(shCore, "GetDpiForMonitor");

			if (GetDpiForMonitor)
			{
				UINT dpiX, dpiY;

				POINT point;
				point.x = 1;
				point.y = 1;

				// Get DPI for the main monitor
				HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
				GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
				g_dpi.SetScale(dpiX, dpiY);
			}

			FreeLibrary(shCore);
		}
	}

	static bool lastTen = g_uui.tenMode;

	if (safeMode)
	{
		lastTen = g_uui.tenMode;
		g_uui.tenMode = false;
	}
	else
	{
		g_uui.tenMode = lastTen;
	}

	UI_RegisterClass();
	UI_CreateWindow();
}

void UI_DoDestruction()
{
	static HostSharedData<CfxState> initState("CfxInitState");
	AllowSetForegroundWindow((initState->gamePid) ? initState->gamePid : GetCurrentProcessId());

	ShowWindow(g_uui.rootWindow, SW_HIDE);

	g_uui.ten = {};

	DestroyWindow(g_uui.rootWindow);
}

void UI_SetSnailState(bool snail)
{
	if (g_uui.ten)
	{
		g_uui.ten->snailContainer.Visibility(snail ? winrt::Windows::UI::Xaml::Visibility::Visible : winrt::Windows::UI::Xaml::Visibility::Collapsed);

		return;
	}
}

void UI_UpdateText(int textControl, const wchar_t* text)
{
	if (g_uui.ten)
	{
		std::wstring tstr = text;

		if (textControl == 0)
		{
			g_uui.ten->topStatic.Text(tstr);
		}
		else
		{
			g_uui.ten->bottomStatic.Text(tstr);
		}

		return;
	}

	if (textControl == 0)
	{
		wcscpy(g_uui.topText, text);
	}
	else
	{
		wcscpy(g_uui.bottomText, text);
	}
}

void UI_UpdateProgress(double percentage)
{
	if (g_uui.ten)
	{
		try
		{
			// Calculate width based on percentage (350 is max width)
			double width = (percentage / 100.0) * 350.0;
			g_uui.ten->progressBar.Width(width);
		}
		catch (...)
		{
		}

		return;
	}

	SendMessage(g_uui.progressBar, PBM_SETPOS, (int)(percentage * 100), 0);

	if (g_uui.tbList)
	{
		g_uui.tbList->SetProgressValue(g_uui.rootWindow, (int)percentage, 100);

		if (percentage == 100)
		{
			g_uui.tbList->SetProgressState(g_uui.rootWindow, TBPF_NOPROGRESS);
		}
	}
}

bool UI_IsCanceled()
{
	return g_uui.canceled;
}

void UI_DisplayError(const wchar_t* error)
{
	static TASKDIALOGCONFIG taskDialogConfig = { 0 };
	taskDialogConfig.cbSize = sizeof(taskDialogConfig);
	taskDialogConfig.hInstance = GetModuleHandle(nullptr);
	taskDialogConfig.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_SIZE_TO_CONTENT;
	taskDialogConfig.dwCommonButtons = TDCBF_CLOSE_BUTTON;
	taskDialogConfig.pszWindowTitle = L"Error updating " PRODUCT_NAME;
	taskDialogConfig.pszMainIcon = TD_ERROR_ICON;
	taskDialogConfig.pszMainInstruction = NULL;
	taskDialogConfig.pszContent = error;

	TaskDialogIndirect(&taskDialogConfig, nullptr, nullptr, nullptr);
}

#include <wrl/module.h>

extern "C" HRESULT __stdcall DllCanUnloadNow()
{
#ifdef _WRL_MODULE_H_
	if (!::Microsoft::WRL::Module<::Microsoft::WRL::InProc>::GetModule().Terminate())
	{
		return 1; // S_FALSE
	}
#endif

	if (winrt::get_module_lock())
	{
		return 1; // S_FALSE
	}

	winrt::clear_factory_cache();
	return 0; // S_OK
}

extern "C" DLL_EXPORT HRESULT WINAPI DllGetActivationFactory(HSTRING classId, IActivationFactory** factory)
{
	try
	{
		*factory = nullptr;
		uint32_t length{};
		wchar_t const* const buffer = WindowsGetStringRawBuffer(classId, &length);
		std::wstring_view const name{ buffer, length };

		auto requal = [](std::wstring_view const& left, std::wstring_view const& right) noexcept
		{
			return std::equal(left.rbegin(), left.rend(), right.rbegin(), right.rend());
		};

		if (requal(name, L"CitiLaunch.BackdropBrush"))
		{
			*factory = (IActivationFactory*)winrt::detach_abi(winrt::make<BackdropBrush>());
			return 0;
		}

#ifdef _WRL_MODULE_H_
		return ::Microsoft::WRL::Module<::Microsoft::WRL::InProc>::GetModule().GetActivationFactory(static_cast<HSTRING>(classId), reinterpret_cast<::IActivationFactory * *>(factory));
#else
		return winrt::hresult_class_not_available(name).to_abi();
#endif
	}
	catch (...) { return winrt::to_hresult(); }
}
#endif
