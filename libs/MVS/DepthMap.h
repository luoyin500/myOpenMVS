/*
* DepthMap.h
*/

#ifndef _MVS_DEPTHMAP_H_
#define _MVS_DEPTHMAP_H_


// I N C L U D E S /////////////////////////////////////////////////

#include "Image.h"
#include "PointCloud.h"


// D E F I N E S ///////////////////////////////////////////////////

// NCC type used for patch-similarity computation during depth-map estimation
#define DENSE_NCC_DEFAULT 0
#define DENSE_NCC_FAST 1
#define DENSE_NCC DENSE_NCC_FAST

// NCC score aggregation type used during depth-map estimation
#define DENSE_AGGNCC_NTH 0
#define DENSE_AGGNCC_MEAN 1
#define DENSE_AGGNCC_MIN 2
#define DENSE_AGGNCC DENSE_AGGNCC_MEAN

#define ComposeDepthFilePathBase(b, i, e) MAKE_PATH(String::FormatString((b + "%04u." e).c_str(), i))
#define ComposeDepthFilePath(i, e) MAKE_PATH(String::FormatString("depth%04u." e, i))


// S T R U C T S ///////////////////////////////////////////////////

namespace MVS {

DECOPT_SPACE(OPTDENSE)

namespace OPTDENSE {
enum DepthFlags {
	REMOVE_SPECKLES	= (1 << 0),
	FILL_GAPS		= (1 << 1),
	ADJUST_FILTER	= (1 << 2),
	OPTIMIZE		= (REMOVE_SPECKLES|FILL_GAPS)
};
extern unsigned nMinResolution;
extern unsigned nResolutionLevel;
extern unsigned nMinViews;
extern unsigned nMaxViews;
extern unsigned nMinViewsFuse;
extern unsigned nMinViewsFilter;
extern unsigned nMinViewsFilterAdjust;
extern unsigned nMinViewsTrustPoint;
extern unsigned nNumViews;
extern bool bFilterAdjust;
extern bool bAddCorners;
extern float fViewMinScore;
extern float fViewMinScoreRatio;
extern float fMinArea;
extern float fMinAngle;
extern float fOptimAngle;
extern float fMaxAngle;
extern float fDescriptorMinMagnitudeThreshold;
extern float fDepthDiffThreshold;
extern float fPairwiseMul;
extern float fOptimizerEps;
extern int nOptimizerMaxIters;
extern unsigned nSpeckleSize;
extern unsigned nIpolGapSize;
extern unsigned nOptimize;
extern unsigned nEstimateColors;
extern unsigned nEstimateNormals;
extern float fNCCThresholdKeep;
extern float fNCCThresholdRefine;
extern unsigned nEstimationIters;
extern unsigned nRandomIters;
extern unsigned nRandomMaxScale;
extern float fRandomDepthRatio;
extern float fRandomAngle1Range;
extern float fRandomAngle2Range;
extern float fRandomSmoothDepth;
extern float fRandomSmoothNormal;
extern float fRandomSmoothBonus;
} // namespace OPTDENSE
/*----------------------------------------------------------------*/


struct MVS_API DepthData {
	struct ViewData {
		float scale; // 相对于参考图像的图像缩放
		Camera camera; // 对应于该图像的摄像机矩阵
		Image32F image; // 图像浮动强度
		Image* pImageData; // 图像数据

		// 缩放图像
		template <typename IMAGE>
		static bool ScaleImage(const IMAGE& image, IMAGE& imageScaled, float scale) {
			if (ABS(scale-1.f) < 0.15f)
				return false;
			cv::resize(image, imageScaled, cv::Size(), scale, scale, scale>1?cv::INTER_CUBIC:cv::INTER_AREA);	// 插值法 双三次插值  使用像素面积关系重采样。 它可能是图像抽取的优选方法，因为它给出了无莫尔条纹的结果。 但是当图像缩放时，它类似于inter_nearest方法。
			return true;
		}
	};
	typedef SEACAVE::cList<ViewData,const ViewData&,2> ViewDataArr;

	// 用于计算此深度图的图像数组（参考图像是第一个）
	ViewDataArr images;
	// 看到此深度图的所有图像的数组（按重要性递减排序）
	ViewScoreArr neighbors;
	// 此图像看到的稀疏3D点的索引
	IndexArr points;
	// 标记要忽略的像素
	BitMatrix mask;
	// depth-map
	DepthMap depthMap;
	// 相机空间中的法线映射
	NormalMap normalMap;
	// confidence-map
	ConfidenceMap confMap;
	float dMin, dMax; // global depth range for this image
	// 此深度图被引用多少次（在0上可以安全卸载）
	unsigned references;
	// 用于计算引用
	CriticalSection cs;

	inline DepthData() : references(0) {}

	inline void ReleaseImages() {
		FOREACHPTR(ptrImage, images)
			ptrImage->image.release();
	}
	inline void Release() {
		depthMap.release();
		normalMap.release();
		confMap.release();
	}

	inline bool IsValid() const {
		return !images.IsEmpty();
	}
	inline bool IsEmpty() const {
		return depthMap.empty();
	}

	void GetNormal(const ImageRef& ir, Point3f& N, const TImage<Point3f>* pPointMap=NULL) const;
	void GetNormal(const Point2f& x, Point3f& N, const TImage<Point3f>* pPointMap=NULL) const;

	bool Save(const String& fileName) const;
	bool Load(const String& fileName);

	unsigned GetRef();
	unsigned IncRef(const String& fileName);
	unsigned DecRef();

	#ifdef _USE_BOOST
	// implement BOOST serialization
	template<class Archive>
	void serialize(Archive& ar, const unsigned int /*version*/) {
		ASSERT(IsValid());
		ar & depthMap;
		ar & normalMap;
		ar & confMap;
		ar & dMin;
		ar & dMax;
	}
	#endif
};
typedef MVS_API CLISTDEFIDX(DepthData,IIndex) DepthDataArr;
/*----------------------------------------------------------------*/


struct MVS_API DepthEstimator {
	enum { TexelChannels = 1 };
	enum { nSizeHalfWindow = 3 };
	enum { nSizeWindow = nSizeHalfWindow*2+1 };
	enum { nTexels = nSizeWindow*nSizeWindow*TexelChannels };

	enum ENDIRECTION {
		LT2RB = 0,
		RB2LT,
		DIRS
	};

	typedef TPoint2<uint16_t> MapRef;
	typedef CLISTDEF0(MapRef) MapRefArr;

	typedef Eigen::Matrix<float,nTexels,1> TexelVec;
	struct NeighborData {
		Depth depth;
		Normal normal;
		inline NeighborData() {}
		inline NeighborData(Depth d, const Normal& n) : depth(d), normal(n) {}
	};

	struct ViewData {
		const DepthData::ViewData& view;
		const Matrix3x3 Hl;   //
		const Vec3 Hm;	      // constants during per-pixel loops
		const Matrix3x3 Hr;   //
		inline ViewData() : view(*((const DepthData::ViewData*)this)) {}
		inline ViewData(const DepthData::ViewData& image0, const DepthData::ViewData& image1)
			: view(image1),
			Hl(image1.camera.K * image1.camera.R * image0.camera.R.t()),
			Hm(image1.camera.K * image1.camera.R * (image0.camera.C - image1.camera.C)),
			Hr(image0.camera.K.inv()) {}
	};

	CLISTDEF0IDX(NeighborData,IIndex) neighborsData; // 邻接像素深度用于平滑
	CLISTDEF0IDX(ImageRef,IIndex) neighbors; // neighbor pixels coordinates to be processed 要处理的相邻像素坐标
	volatile Thread::safe_t& idxPixel; // current image index to be processed
	Vec3 X0;	      //
	ImageRef x0;	  // 一个像素循环期间的常数
	float normSq0;	  //
	TexelVec texels0; //
	#if DENSE_NCC == DENSE_NCC_DEFAULT
	TexelVec texels1;
	#endif
	#if DENSE_AGGNCC == DENSE_AGGNCC_NTH
	FloatArr scores;
	#else
	Eigen::VectorXf scores;
	#endif
	DepthMap& depthMap0;
	NormalMap& normalMap0;
	ConfidenceMap& confMap0;

	const CLISTDEF0(ViewData) images; // neighbor images used
	const DepthData::ViewData& image0;
	const Image64F& image0Sum; // integral image used to fast compute patch mean intensity
	const MapRefArr& coords;
	const Image8U::Size size;
	#if DENSE_AGGNCC == DENSE_AGGNCC_NTH
	const IDX idxScore;
	#endif
	const ENDIRECTION dir;
	const Depth dMin, dMax;

	DepthEstimator(DepthData& _depthData0, volatile Thread::safe_t& _idx, const Image64F& _image0Sum, const MapRefArr& _coords, ENDIRECTION _dir);

	bool PreparePixelPatch(const ImageRef&);
	bool FillPixelPatch();
	float ScorePixel(Depth, const Normal&);
	void ProcessPixel(IDX idx);
	
	inline float GetImage0Sum(const ImageRef& p) const {
		const ImageRef p0(p.x-nSizeHalfWindow, p.y-nSizeHalfWindow);
		const ImageRef p1(p0.x+nSizeWindow, p0.y);
		const ImageRef p2(p0.x, p0.y+nSizeWindow);
		const ImageRef p3(p0.x+nSizeWindow, p0.y+nSizeWindow);
		return (float)(image0Sum(p3) - image0Sum(p2) - image0Sum(p1) + image0Sum(p0));
	}

	inline Matrix3x3f ComputeHomographyMatrix(const ViewData& img, Depth depth, const Normal& normal) const {
		#if 0
		// compute homography matrix
		const Matrix3x3f H(img.view.camera.K*HomographyMatrixComposition(image0.camera, img.view.camera, Vec3(normal), Vec3(X0*depth))*image0.camera.K.inv());
		#else
		// compute homography matrix as above, caching some constants
		const Vec3 n(normal);
		return (img.Hl + img.Hm * (n.t()*INVERT(n.dot(X0)*depth))) * img.Hr;
		#endif
	}

	static inline CLISTDEF0(ViewData) InitImages(const DepthData& depthData) {
		CLISTDEF0(ViewData) images(0, depthData.images.GetSize()-1);
		const DepthData::ViewData& image0(depthData.images.First());
		for (IDX i=1; i<depthData.images.GetSize(); ++i)
			images.AddConstruct(image0, depthData.images[i]);
		return images;
	}

	static inline Point3 ComputeRelativeC(const DepthData& depthData) {
		return depthData.images[1].camera.R*(depthData.images[0].camera.C-depthData.images[1].camera.C);
	}
	static inline Matrix3x3 ComputeRelativeR(const DepthData& depthData) {
		RMatrix R;
		ComputeRelativeRotation(depthData.images[0].camera.R, depthData.images[1].camera.R, R);
		return R;
	}

	// generate random depth and normal
	static inline Depth RandomDepth(Depth dMin, Depth dMax) {
		ASSERT(dMin > 0);
		return randomRange(dMin, dMax);
	}
	static inline Normal RandomNormal(const Point3f& viewRay) {
		Normal normal;
		Dir2Normal(Point2f(randomRange(FD2R(0.f),FD2R(360.f)), randomRange(FD2R(120.f),FD2R(180.f))), normal);
		return normal.dot(viewRay) > 0 ? -normal : normal;
	}

	// 在一个浮动中编码/解码NCC分数和细化级别
	static inline float EncodeScoreScale(float score, unsigned invScaleRange=0) {	// 编码
		ASSERT(score >= 0.f && score <= 2.01f);
		return score*0.1f+(float)invScaleRange;
	}

	static inline unsigned DecodeScale(float score) {
		return (unsigned)FLOOR2INT(score);
	}
	static inline unsigned DecodeScoreScale(float& score) {	// 解码
		const unsigned invScaleRange(DecodeScale(score));
		score = (score-(float)invScaleRange)*10.f;
		return invScaleRange;
	}

	// Encodes/decodes a normalized 3D vector in two parameters for the direction
	template<typename T, typename TR>
	static inline void Normal2Dir(const TPoint3<T>& d, TPoint2<TR>& p) {
		// empirically tested
		//ASSERT(ISEQUAL(norm(d), T(1)));
		p.y = TR(atan2(sqrt(d.x*d.x + d.y*d.y), d.z));
		p.x = TR(atan2(d.y, d.x));
	}
	template<typename T, typename TR>
	static inline void Dir2Normal(const TPoint2<T>& p, TPoint3<TR>& d) {
		// empirically tested
		const T siny(sin(p.y));
		d.x = TR(cos(p.x)*siny);
		d.y = TR(sin(p.x)*siny);
		d.z = TR(cos(p.y));
		ASSERT(ISEQUAL(norm(d), TR(1)));
	}

	static void MapMatrix2ZigzagIdx(const Image8U::Size& size, DepthEstimator::MapRefArr& coords, BitMatrix& mask, int rawStride=16);

	const float smoothBonusDepth, smoothBonusNormal;
	const float smoothSigmaDepth, smoothSigmaNormal;
	const float thMagnitudeSq;
	const float angle1Range, angle2Range;
	const float thConfSmall, thConfBig;
	const float thRobust;
	static const float scaleRanges[12];
};
/*----------------------------------------------------------------*/


// Tools
MVS_API unsigned EstimatePlane(const Point3Arr&, Plane&, double& maxThreshold, bool arrInliers[]=NULL, size_t maxIters=0);
MVS_API unsigned EstimatePlaneLockFirstPoint(const Point3Arr&, Plane&, double& maxThreshold, bool arrInliers[]=NULL, size_t maxIters=0);
MVS_API unsigned EstimatePlaneTh(const Point3Arr&, Plane&, double maxThreshold, bool arrInliers[]=NULL, size_t maxIters=0);
MVS_API unsigned EstimatePlaneThLockFirstPoint(const Point3Arr&, Plane&, double maxThreshold, bool arrInliers[]=NULL, size_t maxIters=0);

MVS_API void EstimatePointColors(const ImageArr& images, PointCloud& pointcloud);
MVS_API void EstimatePointNormals(const ImageArr& images, PointCloud& pointcloud, int numNeighbors=16/*K-nearest neighbors*/);

MVS_API bool SaveDepthMap(const String& fileName, const DepthMap& depthMap);
MVS_API bool LoadDepthMap(const String& fileName, DepthMap& depthMap);
MVS_API bool SaveNormalMap(const String& fileName, const NormalMap& normalMap);
MVS_API bool LoadNormalMap(const String& fileName, NormalMap& normalMap);
MVS_API bool SaveConfidenceMap(const String& fileName, const ConfidenceMap& confMap);
MVS_API bool LoadConfidenceMap(const String& fileName, ConfidenceMap& confMap);

MVS_API bool ExportDepthMap(const String& fileName, const DepthMap& depthMap, Depth minDepth=FLT_MAX, Depth maxDepth=0);
MVS_API bool ExportNormalMap(const String& fileName, const NormalMap& normalMap);
MVS_API bool ExportConfidenceMap(const String& fileName, const ConfidenceMap& confMap);
MVS_API bool ExportPointCloud(const String& fileName, const Image&, const DepthMap&, const NormalMap&);

MVS_API void CompareDepthMaps(const DepthMap& depthMap, const DepthMap& depthMapGT, uint32_t idxImage, float threshold=0.01f);
MVS_API void CompareNormalMaps(const NormalMap& normalMap, const NormalMap& normalMapGT, uint32_t idxImage);
/*----------------------------------------------------------------*/

} // namespace MVS

#endif // _MVS_DEPTHMAP_H_
