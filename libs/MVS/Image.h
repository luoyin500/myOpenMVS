/*
* Image.h
*/

#ifndef _MVS_VIEW_H_
#define _MVS_VIEW_H_


// I N C L U D E S /////////////////////////////////////////////////

#include "Platform.h"


// D E F I N E S ///////////////////////////////////////////////////


// S T R U C T S ///////////////////////////////////////////////////

namespace MVS {

typedef uint32_t IIndex;
typedef cList<IIndex, IIndex, 0, 16, IIndex> IIndexArr;

struct MVS_API ViewInfo {
	IIndex ID; // image ID
	uint32_t points; // number of 3D points shared with the reference image 与参考图像共享的3D点的数目
	float scale; // image scale relative to the reference image 相对于参考图像的图像缩放
	float angle; // image angle relative to the reference image (radians) 相对于参考图像的图像角度（弧度）
	float area; // common image area relative to the reference image (ratio) 相对于参考图像的公共图像区域（比率）

	#ifdef _USE_BOOST
				// implement BOOST serialization
	template<class Archive>
	void serialize(Archive& ar, const unsigned int /*version*/) {
		ar & ID;
		ar & points;
		ar & scale;
		ar & angle;
		ar & area;
	}
	#endif
};
typedef MVS_API TIndexScore<ViewInfo, float> ViewScore;
typedef MVS_API CLISTDEF0IDX(ViewScore, IIndex) ViewScoreArr;
/*----------------------------------------------------------------*/

// a view instance seeing the scene 查看场景的视图实例
class MVS_API Image
{
public:
	uint32_t platformID; // ID of the associated platform
	uint32_t cameraID; // ID of the associated camera on the associated platform 关联平台上的关联摄像机的ID
	uint32_t poseID; // ID of the pose of the associated platform
	String name; // image file name (relative path)	相对路径
	Camera camera; // view's pose
	uint32_t width, height; // image size
	Image8U3 image; // 图像颜色像素
	ViewScoreArr neighbors; // score&store the neighbor images 记分和存储邻居图像
	float scale; // image scale relative to the original size 相对于原始大小的图像缩放
	float avgDepth; // average depth of the points seen by this camera 此相机所见点的平均深度

public:
	inline Image() {
		#ifndef _RELEASE
		width = height = 0;
		#endif
	}

	inline bool IsValid() const { return poseID != NO_ID; }
	inline Image8U::Size GetSize() const { return Image8U::Size(width, height); }

	// read image data from the file
	static IMAGEPTR OpenImage(const String& fileName);
	static IMAGEPTR ReadImageHeader(const String& fileName);
	static IMAGEPTR ReadImage(const String& fileName, Image8U3& image);
	static bool ReadImage(IMAGEPTR pImage, Image8U3& image);
	bool LoadImage(const String& fileName, unsigned nMaxResolution=0);
	bool ReloadImage(unsigned nMaxResolution=0, bool bLoadPixels=true);
	void ReleaseImage();
	float ResizeImage(unsigned nMaxResolution=0);
	unsigned RecomputeMaxResolution(unsigned& level, unsigned minImageSize) const;

	Camera GetCamera(const PlatformArr& platforms, const Image8U::Size& resolution) const;
	void UpdateCamera(const PlatformArr& platforms);

	float GetNormalizationScale() const {
		ASSERT(width > 0 && height > 0);
		return camera.GetNormalizationScale(width, height);
	}

	#ifdef _USE_BOOST
	// implement BOOST serialization
	template<class Archive>
	void save(Archive& ar, const unsigned int /*version*/) const {
		ar & platformID;
		ar & cameraID;
		ar & poseID;
		const String relName(MAKE_PATH_REL(WORKING_FOLDER_FULL, name));
		ar & relName;
		ar & width & height;
		ar & neighbors;
		ar & avgDepth;
	}
	template<class Archive>
	void load(Archive& ar, const unsigned int /*version*/) {
		ar & platformID;
		ar & cameraID;
		ar & poseID;
		ar & name;
		name = MAKE_PATH_FULL(WORKING_FOLDER_FULL, name);
		ar & width & height;
		ar & neighbors;
		ar & avgDepth;
	}
	BOOST_SERIALIZATION_SPLIT_MEMBER()
	#endif
};
typedef MVS_API SEACAVE::cList<Image, const Image&, 2, 16, IIndex> ImageArr;
/*----------------------------------------------------------------*/

} // namespace MVS

#endif // _MVS_VIEW_H_
