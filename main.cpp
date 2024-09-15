
#include <stdio.h>
#include <stdint.h>
#include <cmath>
#include <memory>
#include <windows.h>
#pragma comment(lib, "gdiplus.lib")
#define GDIPVER 0x0110
#include <gdiplus.h>

struct GdiplusInitlizeStruct {
	Gdiplus::GdiplusStartupInput input;
	Gdiplus::GdiplusStartupOutput output;
	ULONG_PTR token;
	GdiplusInitlizeStruct() {
		Gdiplus::GdiplusStartup(&token, &input, &output);
	}
	~GdiplusInitlizeStruct() {
		Gdiplus::GdiplusShutdown(token);
	}
};

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
{
	UINT  num = 0;          // number of image encoders
	UINT  size = 0;         // size of the image encoder array in bytes

	Gdiplus::ImageCodecInfo* pImageCodecInfo = NULL;

	Gdiplus::GetImageEncodersSize(&num, &size);
	if (size == 0)
		return -1;  // Failure

	pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
	if (pImageCodecInfo == NULL)
		return -1;  // Failure

	Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

	for (UINT j = 0; j < num; ++j)
	{
		if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0)
		{
			*pClsid = pImageCodecInfo[j].Clsid;
			free(pImageCodecInfo);
			return j;  // Success
		}
	}

	free(pImageCodecInfo);
	return -1;  // Failure
}

static constexpr uint8_t rounddoubletou8(double f) {
	f += 0.5;
	if (f >= 255) return 255;
	if (f <= 0) return 0;
	return (uint8_t)f;
}

struct doublecolor {
	double alpha, pred, pgreen, pblue;
	constexpr doublecolor() : alpha(0), pred(0), pgreen(0), pblue(0) {}
	constexpr doublecolor(uint32_t pcolor) :
		alpha((uint8_t)(pcolor >> 24) * (1.0 / 255)),
		pred((uint8_t)(pcolor >> 16) * (1.0 / 255) * alpha),
		pgreen((uint8_t)(pcolor >> 8) * (1.0 / 255) * alpha),
		pblue((uint8_t)pcolor * (1.0 / 255) * alpha) {}
	constexpr doublecolor(const doublecolor& color) :
		alpha(color.alpha),
		pred(color.pred),
		pgreen(color.pgreen),
		pblue(color.pblue) {}
	constexpr doublecolor(double _alpha, double _pred, double _pgreen, double _pblue) :
		alpha(_alpha), pred(_pred), pgreen(_pgreen), pblue(_pblue) {}
	constexpr operator uint32_t() const {
		return ((uint32_t)rounddoubletou8(alpha * 255) << 24)
			| ((uint32_t)rounddoubletou8(pred / alpha * 255) << 16)
			| ((uint32_t)rounddoubletou8(pgreen / alpha * 255) << 8)
			| (uint32_t)rounddoubletou8(pblue / alpha * 255);
	}
	//将此颜色盖到另一颜色上得到输出颜色
	constexpr doublecolor cover(doublecolor bkcolor) const {
		bkcolor.pred = pred + bkcolor.pred * (1.0 - alpha);
		bkcolor.pgreen = pgreen + bkcolor.pgreen * (1.0 - alpha);
		bkcolor.pblue = pblue + bkcolor.pblue * (1.0 - alpha);
		bkcolor.alpha = alpha + bkcolor.alpha * (1.0 - alpha);
		return bkcolor;
	}
	constexpr void fromARGB(Gdiplus::ARGB input) {
		this->doublecolor::doublecolor(input);
	}
	constexpr doublecolor& operator=(const doublecolor& color) {
		alpha = color.alpha;
		pred = color.pred;
		pgreen = color.pgreen;
		pblue = color.pblue;
		return *this;
	}
	constexpr doublecolor& operator+=(const doublecolor& color) {
		alpha += color.alpha;
		pred += color.pred;
		pgreen += color.pgreen;
		pblue += color.pblue;
		return *this;
	}
	constexpr doublecolor operator+(doublecolor color) const{
		color += *this;
		return color;
	}
	constexpr doublecolor operator-() {
		doublecolor color(-alpha, -pred, -pgreen, -pblue);
		return color;
	}
	constexpr doublecolor& operator-=(const doublecolor& color) {
		alpha -= color.alpha;
		pred -= color.pred;
		pgreen -= color.pgreen;
		pblue -= color.pblue;
		return *this;
	}
	constexpr doublecolor operator-(doublecolor color) const {
		doublecolor ret(*this);
		ret -= color;
		return ret;
	}
	constexpr doublecolor& operator*=(const doublecolor& color) {
		alpha *= color.alpha;
		pred *= color.pred;
		pgreen *= color.pgreen;
		pblue *= color.pblue;
		return *this;
	}
	constexpr doublecolor operator*(doublecolor color) const {
		color *= *this;
		return color;
	}
	constexpr doublecolor& operator/=(const doublecolor& color) {
		alpha /= color.alpha;
		pred /= color.pred;
		pgreen /= color.pgreen;
		pblue /= color.pblue;
		return *this;
	}
	constexpr doublecolor operator/(doublecolor color) const {
		doublecolor ret(*this);
		ret /= color;
		return ret;
	}
};

static void getRealColorWork(double c1, double c2, double _c1, double _c2, double& res, double& valweight, double& resalpha) {
	double f;
	f = c1 - c2;
	if (f != 0) {
		res = (c1 * _c2 - c2 * _c1) / f;
		valweight += std::abs(f);
		resalpha += (1.0 - (_c1 - _c2) / f) * std::abs(f);
	}
	else {
		res = c1;
	}
}

//输入结果色1，结果色2，背景色1，背景色2获取原本的颜色
static uint32_t getRealColor(uint32_t color1, uint32_t color2, uint32_t bkcolor1, uint32_t bkcolor2) {
	doublecolor c1(bkcolor1), c2(bkcolor2), _c1(color1), _c2(color2);
	doublecolor ret;
	double valweight = 0;
	double resalpha = 0;
	getRealColorWork(c1.alpha, c2.alpha, _c1.alpha, _c2.alpha, ret.alpha, valweight, resalpha);
	getRealColorWork(c1.pred, c2.pred, _c1.pred, _c2.pred, ret.pred, valweight, resalpha);
	getRealColorWork(c1.pgreen, c2.pgreen, _c1.pgreen, _c2.pgreen, ret.pgreen, valweight, resalpha);
	getRealColorWork(c1.pblue, c2.pblue, _c1.pblue, _c2.pblue, ret.pblue, valweight, resalpha);
	if (valweight) {
		ret.alpha = resalpha / valweight;
	}
	return ret;
}

int main() {
	GdiplusInitlizeStruct GdiplusInit;
	wchar_t buffer[MAX_PATH + 1];
	Gdiplus::ARGB color;
	printf("请输入图片1路径：");
	scanf_s("%w[^\n]%*c", buffer, MAX_PATH + 1);
	std::unique_ptr<Gdiplus::Bitmap> bmp1(Gdiplus::Bitmap::FromFile(buffer));
	if (bmp1->GetLastStatus() != Gdiplus::Ok) {
		printf("打开图片1失败！");
		return 0;
	}
	printf("请输入图片2路径：");
	scanf_s("%w[^\n]%*c", buffer, MAX_PATH + 1);
	std::unique_ptr<Gdiplus::Bitmap> bmp2(Gdiplus::Bitmap::FromFile(buffer));
	if (bmp2->GetLastStatus() != Gdiplus::Ok) {
		printf("打开图片1失败！");
		return 0;
	}

	if (bmp1->GetWidth() != bmp2->GetWidth() || bmp1->GetHeight() != bmp2->GetHeight()) {
		printf("失败！图片长宽不一致");
		return 0;
	}

	printf("请输入图片1中的背景色(16进制，ARGB，如ffff0000表示完全不透明的红色):");
	scanf_s("%x", &color);
	Gdiplus::ARGB color1 = color;
	printf("请输入图片2中的背景色(16进制，ARGB，如ffff0000表示完全不透明的红色):");
	scanf_s("%x", &color);
	Gdiplus::ARGB color2 = color;

	printf("生成输出图片中...\n");
	Gdiplus::Rect rect = { 0, 0, (int)bmp1->GetWidth(), (int)bmp2->GetHeight() };
	Gdiplus::Bitmap bmp(rect.Width, rect.Height, PixelFormat32bppARGB);
	Gdiplus::BitmapData bmpdata, bmp1data, bmp2data;
	bmp.LockBits(&rect, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bmpdata);
	bmp1->LockBits(&rect, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bmp1data);
	bmp2->LockBits(&rect, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bmp2data);
	for (size_t a = 0; a < rect.Height; ++a) {
		uint32_t* result = (uint32_t*)((uint8_t*)bmpdata.Scan0 + bmpdata.Stride * a)
			, * input1 = (uint32_t*)((uint8_t*)bmp1data.Scan0 + bmp1data.Stride * a)
			, * input2 = (uint32_t*)((uint8_t*)bmp2data.Scan0 + bmp2data.Stride * a);
		for (size_t b = 0; b < rect.Width; ++b) {
			/*if (a == 843 && b == 1036)
				__debugbreak();*/
			*result++ = getRealColor(*input1++, *input2++, color1, color2);
		}
	}
	bmp2->UnlockBits(&bmp2data);
	bmp1->UnlockBits(&bmp1data);
	bmp.UnlockBits(&bmpdata);
	printf("保存输出图片中...\n");
	CLSID encoderClsid;
	int result;
	result = GetEncoderClsid(L"image/png", &encoderClsid);
	if (result < 0){
		printf("The PNG encoder is not installed.\n");
		return 0;
	}
	bmp.Save(L"output.png", &encoderClsid, nullptr);
	printf("完成！\n");
}
