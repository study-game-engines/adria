#pragma once
#define __d3d12_h__
#include "SimpleMath.h"

namespace adria
{
	using BoundingBox = DirectX::BoundingBox;
	using OrientedBoundingBox = DirectX::BoundingOrientedBox;
	using BoundingFrustum = DirectX::BoundingFrustum;
	using BoundingSphere = DirectX::BoundingSphere;
	using Vector2 = DirectX::SimpleMath::Vector2;
	using Vector3 = DirectX::SimpleMath::Vector3;
	using Vector4 = DirectX::SimpleMath::Vector4;
	using Matrix = DirectX::SimpleMath::Matrix;
	using Quaternion = DirectX::SimpleMath::Quaternion;
	using Color = DirectX::SimpleMath::Color;
	using Ray = DirectX::SimpleMath::Ray;

	using Vector2u = DirectX::XMUINT2;
	using Vector3u = DirectX::XMUINT3;
	using Vector4u = DirectX::XMUINT4;
	using Vector2i = DirectX::XMINT2;
	using Vector3i = DirectX::XMINT3;
	using Vector4i = DirectX::XMINT4;

	namespace colors
	{
		static constexpr Color AliceBlue = { 0.941176534f, 0.972549081f, 1.000000000f, 1.000000000f };
		static constexpr Color AntiqueWhite = { 0.980392218f, 0.921568692f, 0.843137324f, 1.000000000f };
		static constexpr Color Aqua = { 0.000000000f, 1.000000000f, 1.000000000f, 1.000000000f };
		static constexpr Color Aquamarine = { 0.498039246f, 1.000000000f, 0.831372619f, 1.000000000f };
		static constexpr Color Azure = { 0.941176534f, 1.000000000f, 1.000000000f, 1.000000000f };
		static constexpr Color Beige = { 0.960784376f, 0.960784376f, 0.862745166f, 1.000000000f };
		static constexpr Color Bisque = { 1.000000000f, 0.894117713f, 0.768627524f, 1.000000000f };
		static constexpr Color Black = { 0.000000000f, 0.000000000f, 0.000000000f, 1.000000000f };
		static constexpr Color BlanchedAlmond = { 1.000000000f, 0.921568692f, 0.803921640f, 1.000000000f };
		static constexpr Color Blue = { 0.000000000f, 0.000000000f, 1.000000000f, 1.000000000f };
		static constexpr Color BlueViolet = { 0.541176498f, 0.168627456f, 0.886274576f, 1.000000000f };
		static constexpr Color Brown = { 0.647058845f, 0.164705887f, 0.164705887f, 1.000000000f };
		static constexpr Color BurlyWood = { 0.870588303f, 0.721568644f, 0.529411793f, 1.000000000f };
		static constexpr Color CadetBlue = { 0.372549027f, 0.619607866f, 0.627451003f, 1.000000000f };
		static constexpr Color Chartreuse = { 0.498039246f, 1.000000000f, 0.000000000f, 1.000000000f };
		static constexpr Color Chocolate = { 0.823529482f, 0.411764741f, 0.117647067f, 1.000000000f };
		static constexpr Color Coral = { 1.000000000f, 0.498039246f, 0.313725501f, 1.000000000f };
		static constexpr Color CornflowerBlue = { 0.392156899f, 0.584313750f, 0.929411829f, 1.000000000f };
		static constexpr Color Cornsilk = { 1.000000000f, 0.972549081f, 0.862745166f, 1.000000000f };
		static constexpr Color Crimson = { 0.862745166f, 0.078431375f, 0.235294133f, 1.000000000f };
		static constexpr Color Cyan = { 0.000000000f, 1.000000000f, 1.000000000f, 1.000000000f };
		static constexpr Color DarkBlue = { 0.000000000f, 0.000000000f, 0.545098066f, 1.000000000f };
		static constexpr Color DarkCyan = { 0.000000000f, 0.545098066f, 0.545098066f, 1.000000000f };
		static constexpr Color DarkGoldenrod = { 0.721568644f, 0.525490224f, 0.043137256f, 1.000000000f };
		static constexpr Color DarkGray = { 0.662745118f, 0.662745118f, 0.662745118f, 1.000000000f };
		static constexpr Color DarkGreen = { 0.000000000f, 0.392156899f, 0.000000000f, 1.000000000f };
		static constexpr Color DarkKhaki = { 0.741176486f, 0.717647076f, 0.419607878f, 1.000000000f };
		static constexpr Color DarkMagenta = { 0.545098066f, 0.000000000f, 0.545098066f, 1.000000000f };
		static constexpr Color DarkOliveGreen = { 0.333333343f, 0.419607878f, 0.184313729f, 1.000000000f };
		static constexpr Color DarkOrange = { 1.000000000f, 0.549019635f, 0.000000000f, 1.000000000f };
		static constexpr Color DarkOrchid = { 0.600000024f, 0.196078449f, 0.800000072f, 1.000000000f };
		static constexpr Color DarkRed = { 0.545098066f, 0.000000000f, 0.000000000f, 1.000000000f };
		static constexpr Color DarkSalmon = { 0.913725555f, 0.588235319f, 0.478431404f, 1.000000000f };
		static constexpr Color DarkSeaGreen = { 0.560784340f, 0.737254918f, 0.545098066f, 1.000000000f };
		static constexpr Color DarkSlateBlue = { 0.282352954f, 0.239215702f, 0.545098066f, 1.000000000f };
		static constexpr Color DarkSlateGray = { 0.184313729f, 0.309803933f, 0.309803933f, 1.000000000f };
		static constexpr Color DarkTurquoise = { 0.000000000f, 0.807843208f, 0.819607913f, 1.000000000f };
		static constexpr Color DarkViolet = { 0.580392182f, 0.000000000f, 0.827451050f, 1.000000000f };
		static constexpr Color DeepPink = { 1.000000000f, 0.078431375f, 0.576470613f, 1.000000000f };
		static constexpr Color DeepSkyBlue = { 0.000000000f, 0.749019623f, 1.000000000f, 1.000000000f };
		static constexpr Color DimGray = { 0.411764741f, 0.411764741f, 0.411764741f, 1.000000000f };
		static constexpr Color DodgerBlue = { 0.117647067f, 0.564705908f, 1.000000000f, 1.000000000f };
		static constexpr Color Firebrick = { 0.698039234f, 0.133333340f, 0.133333340f, 1.000000000f };
		static constexpr Color FloralWhite = { 1.000000000f, 0.980392218f, 0.941176534f, 1.000000000f };
		static constexpr Color ForestGreen = { 0.133333340f, 0.545098066f, 0.133333340f, 1.000000000f };
		static constexpr Color Fuchsia = { 1.000000000f, 0.000000000f, 1.000000000f, 1.000000000f };
		static constexpr Color Gainsboro = { 0.862745166f, 0.862745166f, 0.862745166f, 1.000000000f };
		static constexpr Color GhostWhite = { 0.972549081f, 0.972549081f, 1.000000000f, 1.000000000f };
		static constexpr Color Gold = { 1.000000000f, 0.843137324f, 0.000000000f, 1.000000000f };
		static constexpr Color Goldenrod = { 0.854902029f, 0.647058845f, 0.125490203f, 1.000000000f };
		static constexpr Color Gray = { 0.501960814f, 0.501960814f, 0.501960814f, 1.000000000f };
		static constexpr Color Green = { 0.000000000f, 0.501960814f, 0.000000000f, 1.000000000f };
		static constexpr Color GreenYellow = { 0.678431392f, 1.000000000f, 0.184313729f, 1.000000000f };
		static constexpr Color Honeydew = { 0.941176534f, 1.000000000f, 0.941176534f, 1.000000000f };
		static constexpr Color HotPink = { 1.000000000f, 0.411764741f, 0.705882370f, 1.000000000f };
		static constexpr Color IndianRed = { 0.803921640f, 0.360784322f, 0.360784322f, 1.000000000f };
		static constexpr Color Indigo = { 0.294117659f, 0.000000000f, 0.509803951f, 1.000000000f };
		static constexpr Color Ivory = { 1.000000000f, 1.000000000f, 0.941176534f, 1.000000000f };
		static constexpr Color Khaki = { 0.941176534f, 0.901960850f, 0.549019635f, 1.000000000f };
		static constexpr Color Lavender = { 0.901960850f, 0.901960850f, 0.980392218f, 1.000000000f };
		static constexpr Color LavenderBlush = { 1.000000000f, 0.941176534f, 0.960784376f, 1.000000000f };
		static constexpr Color LawnGreen = { 0.486274540f, 0.988235354f, 0.000000000f, 1.000000000f };
		static constexpr Color LemonChiffon = { 1.000000000f, 0.980392218f, 0.803921640f, 1.000000000f };
		static constexpr Color LightBlue = { 0.678431392f, 0.847058892f, 0.901960850f, 1.000000000f };
		static constexpr Color LightCoral = { 0.941176534f, 0.501960814f, 0.501960814f, 1.000000000f };
		static constexpr Color LightCyan = { 0.878431439f, 1.000000000f, 1.000000000f, 1.000000000f };
		static constexpr Color LightGoldenrodYellow = { 0.980392218f, 0.980392218f, 0.823529482f, 1.000000000f };
		static constexpr Color LightGreen = { 0.564705908f, 0.933333397f, 0.564705908f, 1.000000000f };
		static constexpr Color LightGray = { 0.827451050f, 0.827451050f, 0.827451050f, 1.000000000f };
		static constexpr Color LightPink = { 1.000000000f, 0.713725507f, 0.756862819f, 1.000000000f };
		static constexpr Color LightSalmon = { 1.000000000f, 0.627451003f, 0.478431404f, 1.000000000f };
		static constexpr Color LightSeaGreen = { 0.125490203f, 0.698039234f, 0.666666687f, 1.000000000f };
		static constexpr Color LightSkyBlue = { 0.529411793f, 0.807843208f, 0.980392218f, 1.000000000f };
		static constexpr Color LightSlateGray = { 0.466666698f, 0.533333361f, 0.600000024f, 1.000000000f };
		static constexpr Color LightSteelBlue = { 0.690196097f, 0.768627524f, 0.870588303f, 1.000000000f };
		static constexpr Color LightYellow = { 1.000000000f, 1.000000000f, 0.878431439f, 1.000000000f };
		static constexpr Color Lime = { 0.000000000f, 1.000000000f, 0.000000000f, 1.000000000f };
		static constexpr Color LimeGreen = { 0.196078449f, 0.803921640f, 0.196078449f, 1.000000000f };
		static constexpr Color Linen = { 0.980392218f, 0.941176534f, 0.901960850f, 1.000000000f };
		static constexpr Color Magenta = { 1.000000000f, 0.000000000f, 1.000000000f, 1.000000000f };
		static constexpr Color Maroon = { 0.501960814f, 0.000000000f, 0.000000000f, 1.000000000f };
		static constexpr Color MediumAquamarine = { 0.400000036f, 0.803921640f, 0.666666687f, 1.000000000f };
		static constexpr Color MediumBlue = { 0.000000000f, 0.000000000f, 0.803921640f, 1.000000000f };
		static constexpr Color MediumOrchid = { 0.729411781f, 0.333333343f, 0.827451050f, 1.000000000f };
		static constexpr Color MediumPurple = { 0.576470613f, 0.439215720f, 0.858823597f, 1.000000000f };
		static constexpr Color MediumSeaGreen = { 0.235294133f, 0.701960802f, 0.443137288f, 1.000000000f };
		static constexpr Color MediumSlateBlue = { 0.482352972f, 0.407843173f, 0.933333397f, 1.000000000f };
		static constexpr Color MediumSpringGreen = { 0.000000000f, 0.980392218f, 0.603921592f, 1.000000000f };
		static constexpr Color MediumTurquoise = { 0.282352954f, 0.819607913f, 0.800000072f, 1.000000000f };
		static constexpr Color MediumVioletRed = { 0.780392230f, 0.082352944f, 0.521568656f, 1.000000000f };
		static constexpr Color MidnightBlue = { 0.098039225f, 0.098039225f, 0.439215720f, 1.000000000f };
		static constexpr Color MintCream = { 0.960784376f, 1.000000000f, 0.980392218f, 1.000000000f };
		static constexpr Color MistyRose = { 1.000000000f, 0.894117713f, 0.882353008f, 1.000000000f };
		static constexpr Color Moccasin = { 1.000000000f, 0.894117713f, 0.709803939f, 1.000000000f };
		static constexpr Color NavajoWhite = { 1.000000000f, 0.870588303f, 0.678431392f, 1.000000000f };
		static constexpr Color Navy = { 0.000000000f, 0.000000000f, 0.501960814f, 1.000000000f };
		static constexpr Color OldLace = { 0.992156923f, 0.960784376f, 0.901960850f, 1.000000000f };
		static constexpr Color Olive = { 0.501960814f, 0.501960814f, 0.000000000f, 1.000000000f };
		static constexpr Color OliveDrab = { 0.419607878f, 0.556862772f, 0.137254909f, 1.000000000f };
		static constexpr Color Orange = { 1.000000000f, 0.647058845f, 0.000000000f, 1.000000000f };
		static constexpr Color OrangeRed = { 1.000000000f, 0.270588249f, 0.000000000f, 1.000000000f };
		static constexpr Color Orchid = { 0.854902029f, 0.439215720f, 0.839215755f, 1.000000000f };
		static constexpr Color PaleGoldenrod = { 0.933333397f, 0.909803987f, 0.666666687f, 1.000000000f };
		static constexpr Color PaleGreen = { 0.596078455f, 0.984313786f, 0.596078455f, 1.000000000f };
		static constexpr Color PaleTurquoise = { 0.686274529f, 0.933333397f, 0.933333397f, 1.000000000f };
		static constexpr Color PaleVioletRed = { 0.858823597f, 0.439215720f, 0.576470613f, 1.000000000f };
		static constexpr Color PapayaWhip = { 1.000000000f, 0.937254965f, 0.835294187f, 1.000000000f };
		static constexpr Color PeachPuff = { 1.000000000f, 0.854902029f, 0.725490212f, 1.000000000f };
		static constexpr Color Peru = { 0.803921640f, 0.521568656f, 0.247058839f, 1.000000000f };
		static constexpr Color Pink = { 1.000000000f, 0.752941251f, 0.796078503f, 1.000000000f };
		static constexpr Color Plum = { 0.866666734f, 0.627451003f, 0.866666734f, 1.000000000f };
		static constexpr Color PowderBlue = { 0.690196097f, 0.878431439f, 0.901960850f, 1.000000000f };
		static constexpr Color Purple = { 0.501960814f, 0.000000000f, 0.501960814f, 1.000000000f };
		static constexpr Color Red = { 1.000000000f, 0.000000000f, 0.000000000f, 1.000000000f };
		static constexpr Color RosyBrown = { 0.737254918f, 0.560784340f, 0.560784340f, 1.000000000f };
		static constexpr Color RoyalBlue = { 0.254901975f, 0.411764741f, 0.882353008f, 1.000000000f };
		static constexpr Color SaddleBrown = { 0.545098066f, 0.270588249f, 0.074509807f, 1.000000000f };
		static constexpr Color Salmon = { 0.980392218f, 0.501960814f, 0.447058856f, 1.000000000f };
		static constexpr Color SandyBrown = { 0.956862807f, 0.643137276f, 0.376470625f, 1.000000000f };
		static constexpr Color SeaGreen = { 0.180392161f, 0.545098066f, 0.341176480f, 1.000000000f };
		static constexpr Color SeaShell = { 1.000000000f, 0.960784376f, 0.933333397f, 1.000000000f };
		static constexpr Color Sienna = { 0.627451003f, 0.321568638f, 0.176470593f, 1.000000000f };
		static constexpr Color Silver = { 0.752941251f, 0.752941251f, 0.752941251f, 1.000000000f };
		static constexpr Color SkyBlue = { 0.529411793f, 0.807843208f, 0.921568692f, 1.000000000f };
		static constexpr Color SlateBlue = { 0.415686309f, 0.352941185f, 0.803921640f, 1.000000000f };
		static constexpr Color SlateGray = { 0.439215720f, 0.501960814f, 0.564705908f, 1.000000000f };
		static constexpr Color Snow = { 1.000000000f, 0.980392218f, 0.980392218f, 1.000000000f };
		static constexpr Color SpringGreen = { 0.000000000f, 1.000000000f, 0.498039246f, 1.000000000f };
		static constexpr Color SteelBlue = { 0.274509817f, 0.509803951f, 0.705882370f, 1.000000000f };
		static constexpr Color Tan = { 0.823529482f, 0.705882370f, 0.549019635f, 1.000000000f };
		static constexpr Color Teal = { 0.000000000f, 0.501960814f, 0.501960814f, 1.000000000f };
		static constexpr Color Thistle = { 0.847058892f, 0.749019623f, 0.847058892f, 1.000000000f };
		static constexpr Color Tomato = { 1.000000000f, 0.388235331f, 0.278431386f, 1.000000000f };
		static constexpr Color Transparent = { 0.000000000f, 0.000000000f, 0.000000000f, 0.000000000f };
		static constexpr Color Turquoise = { 0.250980407f, 0.878431439f, 0.815686345f, 1.000000000f };
		static constexpr Color Violet = { 0.933333397f, 0.509803951f, 0.933333397f, 1.000000000f };
		static constexpr Color Wheat = { 0.960784376f, 0.870588303f, 0.701960802f, 1.000000000f };
		static constexpr Color White = { 1.000000000f, 1.000000000f, 1.000000000f, 1.000000000f };
		static constexpr Color WhiteSmoke = { 0.960784376f, 0.960784376f, 0.960784376f, 1.000000000f };
		static constexpr Color Yellow = { 1.000000000f, 1.000000000f, 0.000000000f, 1.000000000f };
		static constexpr Color YellowGreen = { 0.603921592f, 0.803921640f, 0.196078449f, 1.000000000f };
	}
}