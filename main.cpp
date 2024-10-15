
#include <stdio.h>
#include <stdint.h>
#include <cmath>
#include <memory>
#include <windows.h>
#include <conio.h>
#include <tuple>
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

wchar_t outputfile[MAX_PATH + 1] = L"output.png";

static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
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
#pragma  warning( push ) 
#pragma  warning( disable: 6385 )
		if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0)
#pragma  warning(  pop  ) 
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
	//������ɫ�ǵ���һ��ɫ�ϵõ������ɫ
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

//������ɫ1�����ɫ2������ɫ1������ɫ2��ȡԭ������ɫ
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

static Gdiplus::Status savepng(Gdiplus::Image& img) {
	CLSID encoderClsid;
	int result;
	result = GetEncoderClsid(L"image/png", &encoderClsid);
	if (result < 0) {
		printf("The PNG encoder is not installed.\n");
		return Gdiplus::GenericError;
	}
	return img.Save(outputfile, &encoderClsid, nullptr);
}

//���û���Ҫһ��·������Ϊbitmap����������ʾ��
static Gdiplus::Bitmap* requestBitmap(const char* text) {
	printf("%s", text);
	while (true) {
		wchar_t buffer[MAX_PATH + 1];
		scanf_s("%w[^\n]%*c", buffer, MAX_PATH + 1);
		std::unique_ptr<Gdiplus::Bitmap> bmp(Gdiplus::Bitmap::FromFile(buffer));
		if (bmp->GetLastStatus() == Gdiplus::Ok) return bmp.release();
		printf("��ʧ�ܣ����������룺");
	}
}

static void regenerateAlpha() {
	Gdiplus::ARGB color;
	std::unique_ptr<Gdiplus::Bitmap> bmp1(requestBitmap("������ͼƬ1·����"));
	std::unique_ptr<Gdiplus::Bitmap> bmp2(requestBitmap("������ͼƬ2·����"));

	if (bmp1->GetWidth() != bmp2->GetWidth() || bmp1->GetHeight() != bmp2->GetHeight()) {
		printf("ʧ�ܣ�ͼƬ����һ��");
		return;
	}

	printf("������ͼƬ1�еı���ɫ(16���ƣ�ARGB����ffff0000��ʾ��ȫ��͸���ĺ�ɫ):");
	scanf_s("%x", &color);
	Gdiplus::ARGB color1 = color;
	printf("������ͼƬ2�еı���ɫ(16���ƣ�ARGB����ffff0000��ʾ��ȫ��͸���ĺ�ɫ):");
	scanf_s("%x", &color);
	Gdiplus::ARGB color2 = color;

	printf("�������ͼƬ��...\n");
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
		for (size_t b = 0; b < rect.Width; ++b)
			*result++ = getRealColor(*input1++, *input2++, color1, color2);
	}
	bmp2->UnlockBits(&bmp2data);
	bmp1->UnlockBits(&bmp1data);
	bmp.UnlockBits(&bmpdata);
	printf("�������ͼƬ��...\n");
	if (savepng(bmp) != Gdiplus::Ok)
		printf("����ʧ�ܣ�\n");
	else printf("��ɣ�\n");
}

static void eraseBlackBack() {
	std::unique_ptr<Gdiplus::Bitmap> bmp(requestBitmap("������ͼƬ·����"));
	Gdiplus::Rect rect = { 0, 0, (int)bmp->GetWidth(), (int)bmp->GetHeight() };
	Gdiplus::BitmapData bmpdata;
	bmp->LockBits(&rect, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bmpdata);
	for (size_t a = 0; a < bmpdata.Height; ++a) {
		uint8_t* data = (uint8_t*)bmpdata.Scan0 + a * bmpdata.Stride;
		for (size_t b = 0; b < bmpdata.Width; ++b) {
#undef max
			uint8_t maxcolor = std::max(data[0], std::max(data[1], data[2]));
			for (int i = 0; i < 3; ++i)
				data[i] = maxcolor ? (uint16_t)data[i] * 255 / maxcolor : 0;
			data[3] = (uint16_t)data[3] * maxcolor / 255;
			data += 4;
		}
	}
	bmp->UnlockBits(&bmpdata);
	printf("�������ͼƬ��...\n");
	if (savepng(*bmp) != Gdiplus::Ok)
		printf("����ʧ�ܣ�\n");
	else printf("��ɣ�\n");
}

int main() {
	GdiplusInitlizeStruct GdiplusInit;
	while (true) {
		printf("�����������õĹ��ܣ�\n");
		printf("0:�˳�\n");
		printf("1:���ܰ���(ռλ����ʵ��δʵ��qwq)\n");
		printf("2:�ָ�ͼƬ͸����\n");
		printf("3:ȥ����ɫ����\n");
	switchagain:
		char c = _getch();
		switch (c) {
		case '0':
			putchar('\n');
			printf("��������˳���\n");
			std::ignore = _getch();
			return 0;
		case '1':
			printf("���ֹ���˵����\n");
			printf("����\"3:\"��\n");
			printf("���Ժ�ɫΪ����������ʾ��ԭͼ��ǰ���¾����ܽ���ͼƬ��͸����\n");
			printf("�������������\n");
			std::ignore = _getch();
			break;
		case '2':
			putchar('\n');
			regenerateAlpha();
			break;
		case '3':
			putchar('\n');
			eraseBlackBack();
			break;
		default:
			printf("\r���Ϸ������룡���������롣");
			goto switchagain;
		}
		putchar('\n');
	}
}
