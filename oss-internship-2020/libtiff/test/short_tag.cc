// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <array>
#include <vector>

#include "check_tag.h"
#include "tiffio.h"

/* sapi functions:
 * TIFFWriteScanline
 * TIFFOpen
 * TIFFClose
 * TIFFGetField (from check_tag.c)
 * TIFFSetField
 */

#define SPP 3  // kSamplePerPixel
static const unsigned short kWidth = 1;
static const unsigned short kLength = 1;
static const unsigned short kBps = 8;
static const unsigned short kPhotometric = PHOTOMETRIC_RGB;
static const unsigned short kRowsPerStrip = 1;
static const unsigned short kPlanarConfig = PLANARCONFIG_CONTIG;

struct single_tag {
  const ttag_t tag;
  const unsigned short value;
};

struct paired_tag {
  const ttag_t tag;
  const std::array<unsigned short, 2> values;
};

static const std::vector<single_tag> short_single_tags = {
    {TIFFTAG_COMPRESSION, COMPRESSION_NONE},
    {TIFFTAG_FILLORDER, FILLORDER_MSB2LSB},
    {TIFFTAG_ORIENTATION, ORIENTATION_BOTRIGHT},
    {TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH},
    {TIFFTAG_MINSAMPLEVALUE, 23},
    {TIFFTAG_MAXSAMPLEVALUE, 241},
    {TIFFTAG_INKSET, INKSET_MULTIINK},
    {TIFFTAG_NUMBEROFINKS, SPP},
    {TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT}};

static const std::vector<paired_tag> short_paired_tags = {
    {TIFFTAG_PAGENUMBER, {1, 1}},
    {TIFFTAG_HALFTONEHINTS, {0, 255}},
    {TIFFTAG_DOTRANGE, {8, 16}},
    {TIFFTAG_YCBCRSUBSAMPLING, {2, 1}}};

TEST(SandboxTest, ShortTag) {
  sapi::StatusOr<std::string> status_or_path =
      sandbox2::CreateNamedTempFileAndClose("short_test.tif");
  ASSERT_THAT(status_or_path, IsOk()) << "Could not create temp file";

  std::string srcfile = sandbox2::file::JoinPath(
      sandbox2::file_util::fileops::GetCWD(), status_or_path.value());

  TiffSapiSandbox sandbox("", srcfile);
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";

  std::array<unsigned char, SPP> buffer = {0, 127, 255};
  sapi::v::Array<unsigned char> buffer_(buffer.data(), SPP);

  sapi::StatusOr<int> status_or_int;
  sapi::StatusOr<TIFF*> status_or_tif;

  TiffApi api(&sandbox);
  sapi::v::ConstCStr srcfile_var(srcfile.c_str());
  sapi::v::ConstCStr w_var("w");

  status_or_tif = api.TIFFOpen(srcfile_var.PtrBefore(), w_var.PtrBefore());
  ASSERT_THAT(status_or_tif, IsOk()) << "Could not open " << srcfile;

  sapi::v::RemotePtr tif(status_or_tif.value());
  ASSERT_THAT(tif.GetValue(), NotNull())
      << "Can't create test TIFF file " << srcfile;

  status_or_int = api.TIFFSetFieldUShort1(&tif, TIFFTAG_IMAGEWIDTH, kWidth);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldUShort1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set ImagekWidth tag";

  status_or_int = api.TIFFSetFieldUShort1(&tif, TIFFTAG_IMAGELENGTH, kLength);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldUShort1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set ImageLength tag";

  status_or_int = api.TIFFSetFieldUShort1(&tif, TIFFTAG_BITSPERSAMPLE, kBps);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldUShort1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set BitsPerSample tag";

  status_or_int = api.TIFFSetFieldUShort1(&tif, TIFFTAG_SAMPLESPERPIXEL, SPP);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldUShort1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "Can't set SamplesPerPixel tag";

  status_or_int =
      api.TIFFSetFieldUShort1(&tif, TIFFTAG_ROWSPERSTRIP, kRowsPerStrip);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldUShort1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set RowsPerStrip tag";

  status_or_int =
      api.TIFFSetFieldUShort1(&tif, TIFFTAG_PLANARCONFIG, kPlanarConfig);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldUShort1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "Can't set PlanarConfiguration tag";

  status_or_int =
      api.TIFFSetFieldUShort1(&tif, TIFFTAG_PHOTOMETRIC, kPhotometric);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldUShort1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "Can't set PhotometricInterpretation tag";

  for (auto& tag : short_single_tags) {
    status_or_int = api.TIFFSetFieldUShort1(&tif, tag.tag, tag.value);
    ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldUShort1 fatal error";
    EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set tag " << tag.tag;
  }

  for (auto& tag : short_paired_tags) {
    status_or_int =
        api.TIFFSetFieldUShort2(&tif, tag.tag, tag.values[0], tag.values[1]);
    ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldUShort2 fatal error";
    EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set tag " << tag.tag;
  }

  status_or_int = api.TIFFWriteScanline(&tif, buffer_.PtrBoth(), 0, 0);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFWriteScanline fatal error";
  ASSERT_THAT(status_or_int.value(), Ne(-1)) << "Can't write image data";

  ASSERT_THAT(api.TIFFClose(&tif), IsOk()) << "TIFFClose fatal error";

  sapi::v::ConstCStr r_var("r");
  status_or_tif = api.TIFFOpen(srcfile_var.PtrBefore(), r_var.PtrBefore());
  ASSERT_THAT(status_or_tif, IsOk()) << "Could not open " << srcfile;

  sapi::v::RemotePtr tif2(status_or_tif.value());
  ASSERT_THAT(tif2.GetValue(), NotNull())
      << "Can't create test TIFF file " << srcfile;

  CheckLongField(api, tif2, TIFFTAG_IMAGEWIDTH, kWidth);
  CheckLongField(api, tif2, TIFFTAG_IMAGELENGTH, kLength);
  CheckShortField(api, tif2, TIFFTAG_BITSPERSAMPLE, kBps);
  CheckShortField(api, tif2, TIFFTAG_PHOTOMETRIC, kPhotometric);
  CheckShortField(api, tif2, TIFFTAG_SAMPLESPERPIXEL, SPP);
  CheckLongField(api, tif2, TIFFTAG_ROWSPERSTRIP, kRowsPerStrip);
  CheckShortField(api, tif2, TIFFTAG_PLANARCONFIG, kPlanarConfig);

  for (auto& tag : short_single_tags) {
    CheckShortField(api, tif2, tag.tag, tag.value);
  }

  for (auto& tag : short_paired_tags) {
    CheckShortPairedField(api, tif2, tag.tag, tag.values);
  }

  ASSERT_THAT(api.TIFFClose(&tif2), IsOk()) << "TIFFClose fatal error";
}
