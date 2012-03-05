/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%               CCCC  H   H   AAA   N   N  N   N  EEEEE   L                   %
%              C      H   H  A   A  NN  N  NN  N  E       L                   %
%              C      HHHHH  AAAAA  N N N  N N N  RRR     L                   %
%              C      H   H  A   A  N  NN  N  NN  E       L                   %
%               CCCC  H   H  A   A  N   N  N   N  EEEEE   LLLLL               %
%                                                                             %
%                                                                             %
%                      MagickCore Image Channel Methods                       %
%                                                                             %
%                              Software Design                                %
%                                John Cristy                                  %
%                               December 2003                                 %
%                                                                             %
%                                                                             %
%  Copyright 1999-2012 ImageMagick Studio LLC, a non-profit organization      %
%  dedicated to making software imaging solutions freely available.           %
%                                                                             %
%  You may not use this file except in compliance with the License.  You may  %
%  obtain a copy of the License at                                            %
%                                                                             %
%    http://www.imagemagick.org/script/license.php                            %
%                                                                             %
%  Unless required by applicable law or agreed to in writing, software        %
%  distributed under the License is distributed on an "AS IS" BASIS,          %
%  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   %
%  See the License for the specific language governing permissions and        %
%  limitations under the License.                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%
%
*/

/*
  Include declarations.
*/
#include "MagickCore/studio.h"
#include "MagickCore/image.h"
#include "MagickCore/list.h"
#include "MagickCore/log.h"
#include "MagickCore/monitor.h"
#include "MagickCore/monitor-private.h"
#include "MagickCore/option.h"
#include "MagickCore/pixel-accessor.h"
#include "MagickCore/token.h"
#include "MagickCore/utility.h"
#include "MagickCore/version.h"

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     C h a n n e l O p e r a t i o n I m a g e                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ChannelOperationImage() applies a channel expression to the specified image.
%  The expression consists of one or more channels, either mnemonic or numeric
%  (e.g. red, 1), separated by certain operation symbols as follows:
%
%    <=>     exchange two channels (e.g. red<=>blue)
%    =>      transfer a channel to another (e.g. red=>green)
%    ,       separate channel operations (e.g. red, green)
%    |       read channels from next input image (e.g. red | green)
%    ;       write channels to next output image (e.g. red; green; blue)
%
%  A channel without a operation symbol implies extract. For example, to create
%  3 grayscale images from the red, green, and blue channels of an image, use:
%
%    -channel-ops "red; green; blue"
%
%  The format of the ChannelOperationImage method is:
%
%      Image *ChannelOperationImage(const Image *image,
%        const char *expression,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o expression: A channel expression.
%
%    o exception: return any errors or warnings in this structure.
%
*/

typedef enum
{
  ExtractChannelOp,
  ExchangeChannelOp,
  TransferChannelOp
} ChannelOperation;

static inline size_t MagickMin(const size_t x,const size_t y)
{
  if (x < y)
    return(x);
  return(y);
}

static MagickBooleanType ChannelImage(Image *destination_image,
  const Image *source_image,const ChannelOperation channel_op,
  const PixelChannel source_channel,const PixelChannel destination_channel,
  ExceptionInfo *exception)
{
  CacheView
    *source_view,
    *destination_view;

  MagickBooleanType
    status;

  size_t
    height;

  ssize_t
    y;

  status=MagickTrue;
  source_view=AcquireCacheView(source_image);
  destination_view=AcquireCacheView(destination_image);
  height=MagickMin(source_image->rows,destination_image->rows);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(status)
#endif
  for (y=0; y < (ssize_t) height; y++)
  {
    register const Quantum
      *restrict p;

    register Quantum
      *restrict q;

    register ssize_t
      x;

    size_t
      width;

    if (status == MagickFalse)
      continue;
    p=GetCacheViewVirtualPixels(source_view,0,y,source_image->columns,1,
      exception);
    q=QueueCacheViewAuthenticPixels(destination_view,0,y,
      destination_image->columns,1,exception);
    if ((p == (const Quantum *) NULL) || (q == (Quantum *) NULL))
      {
        status=MagickFalse;
        continue;
      }
    width=MagickMin(source_image->columns,destination_image->columns);
    for (x=0; x < (ssize_t) width; x++)
    {
      PixelTrait
        destination_traits,
        source_traits;

      ssize_t
        offset;

      source_traits=GetPixelChannelMapTraits(source_image,source_channel);
      destination_traits=GetPixelChannelMapTraits(destination_image,
        destination_channel);
      if ((source_traits == UndefinedPixelTrait) ||
          (destination_traits == UndefinedPixelTrait))
        continue;
      offset=GetPixelChannelMapOffset(source_image,source_channel);
      SetPixelChannel(destination_image,destination_channel,p[offset],q);
      p+=GetPixelChannels(source_image);
      q+=GetPixelChannels(destination_image);
    }
    if (SyncCacheViewAuthenticPixels(destination_view,exception) == MagickFalse)
      status=MagickFalse;
  }
  destination_view=DestroyCacheView(destination_view);
  source_view=DestroyCacheView(source_view);
  return(status);
}

MagickExport Image *ChannelOperationImage(const Image *image,
  const char *expression,ExceptionInfo *exception)
{
#define ChannelOperationImageTag  "ChannelOperation/Image"

  char
    token[MaxTextExtent];

  ChannelOperation
    channel_op;

  const char
    *p;

  const Image
    *source_image;

  Image
    *destination_image;

  PixelChannel
    source_channel,
    destination_channel;

  ssize_t
    channels;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickSignature);
  source_image=image;
  destination_image=CloneImage(source_image,0,0,MagickTrue,exception);
  if (destination_image == (Image *) NULL)
    return((Image *) NULL);
  if (SetImageBackgroundColor(destination_image,exception) == MagickFalse)
    {
      destination_image=GetLastImageInList(destination_image);
      return((Image *) NULL);
    }
  if (expression == (const char *) NULL)
    return(destination_image);
  destination_channel=RedPixelChannel;
  p=(char *) expression;
  GetMagickToken(p,&p,token);
  for (channels=0; *p != '\0'; )
  {
    MagickBooleanType
      status;

    ssize_t
      i;

    /*
      Interpret channel expression.
    */
    if (*token == ',')
      {
        destination_channel=(PixelChannel) ((ssize_t) destination_channel+1);
        GetMagickToken(p,&p,token);
      }
    if (*token == '|')
      {
        if (GetNextImageInList(source_image) != (Image *) NULL)
          source_image=GetNextImageInList(source_image);
        else
          source_image=GetFirstImageInList(source_image);
        GetMagickToken(p,&p,token);
      }
    if (*token == ';')
      {
        Image
          *canvas;

        if (channels == 1)
          {
            destination_image->colorspace=GRAYColorspace;
            InitializePixelChannelMap(destination_image);
          }
        canvas=CloneImage(source_image,0,0,MagickTrue,exception);
        if (canvas == (Image *) NULL)
          {
            destination_image=GetLastImageInList(destination_image);
            return((Image *) NULL);
          }
        AppendImageToList(&destination_image,canvas);
        destination_image=GetLastImageInList(destination_image);
        if (SetImageBackgroundColor(destination_image,exception) == MagickFalse)
          {
            destination_image=GetLastImageInList(destination_image);
            return((Image *) NULL);
          }
        GetMagickToken(p,&p,token);
        channels=0;
        destination_channel=RedPixelChannel;
      }
    i=ParsePixelChannelOption(token);
    if (i < 0)
      {
        (void) ThrowMagickException(exception,GetMagickModule(),OptionError,
          "UnableToParseExpression","`%s'",p);
        destination_image=DestroyImageList(destination_image);
        break;
      }
    source_channel=(PixelChannel) i;
    channel_op=ExtractChannelOp;
    GetMagickToken(p,&p,token);
    if (*token == '<')
      {
        channel_op=ExchangeChannelOp;
        GetMagickToken(p,&p,token);
      }
    if (*token == '=')
      GetMagickToken(p,&p,token);
    if (*token == '>')
      {
        if (channel_op != ExchangeChannelOp)
          channel_op=TransferChannelOp;
        GetMagickToken(p,&p,token);
      }
    if (channel_op != ExtractChannelOp)
      {
        i=ParsePixelChannelOption(token);
        if (i < 0)
          {
            (void) ThrowMagickException(exception,GetMagickModule(),OptionError,
              "UnableToParseExpression","`%s'",p);
            destination_image=DestroyImageList(destination_image);
            break;
          }
        destination_channel=(PixelChannel) i;
      }
    status=ChannelImage(destination_image,source_image,channel_op,
      source_channel,destination_channel,exception);
    if (status == MagickFalse)
      {
        destination_image=DestroyImageList(destination_image);
        break;
      }
    channels++;
    status=SetImageProgress(source_image,ChannelOperationImageTag,p-expression,
      strlen(expression));
    if (status == MagickFalse)
      break;
  }
  if (channels == 1)
    {
      destination_image->colorspace=GRAYColorspace;
      InitializePixelChannelMap(destination_image);
    }
  return(destination_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     C o m b i n e I m a g e s                                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  CombineImages() combines one or more images into a single image.  The
%  grayscale value of the pixels of each image in the sequence is assigned in
%  order to the specified channels of the combined image.   The typical
%  ordering would be image 1 => Red, 2 => Green, 3 => Blue, etc.
%
%  The format of the CombineImages method is:
%
%      Image *CombineImages(const Image *image,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport Image *CombineImages(const Image *image,ExceptionInfo *exception)
{
#define CombineImageTag  "Combine/Image"

  CacheView
    *combine_view;

  Image
    *combine_image;

  MagickBooleanType
    status;

  MagickOffsetType
    progress;

  ssize_t
    y;

  /*
    Ensure the image are the same size.
  */
  assert(image != (const Image *) NULL);
  assert(image->signature == MagickSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickSignature);
  combine_image=CloneImage(image,0,0,MagickTrue,exception);
  if (combine_image == (Image *) NULL)
    return((Image *) NULL);
  if (SetImageStorageClass(combine_image,DirectClass,exception) == MagickFalse)
    {
      combine_image=DestroyImage(combine_image);
      return((Image *) NULL);
    }
  if ((GetPixelAlphaTraits(image) & UpdatePixelTrait) != 0)
    combine_image->matte=MagickTrue;
  /*
    Combine images.
  */
  status=MagickTrue;
  progress=0;
  combine_view=AcquireCacheView(combine_image);
  for (y=0; y < (ssize_t) combine_image->rows; y++)
  {
    CacheView
      *image_view;

    const Image
      *next;

    Quantum
      *pixels;

    register const Quantum
      *restrict p;

    register Quantum
      *restrict q;

    register ssize_t
      i;

    if (status == MagickFalse)
      continue;
    pixels=GetCacheViewAuthenticPixels(combine_view,0,y,combine_image->columns,
      1,exception);
    if (pixels == (Quantum *) NULL)
      {
        status=MagickFalse;
        continue;
      }
    next=image;
    for (i=0; i < (ssize_t) GetPixelChannels(image); i++)
    {
      PixelChannel
        channel;

      PixelTrait
        combine_traits,
        traits;

      register ssize_t
        x;

      if (next == (Image *) NULL)
        continue;
      channel=GetPixelChannelMapChannel(image,i);
      traits=GetPixelChannelMapTraits(image,channel);
      combine_traits=GetPixelChannelMapTraits(combine_image,channel);
      if ((traits == UndefinedPixelTrait) ||
          (combine_traits == UndefinedPixelTrait))
        continue;
      image_view=AcquireCacheView(next);
      p=GetCacheViewVirtualPixels(image_view,0,y,next->columns,1,exception);
      if (p == (const Quantum *) NULL)
        continue;
      q=pixels;
      for (x=0; x < (ssize_t) combine_image->columns; x++)
      {
        if (x < (ssize_t) image->columns)
          {
            q[i]=GetPixelGray(image,p);
            p+=GetPixelChannels(image);
          }
        q+=GetPixelChannels(combine_image);
      }
      image_view=DestroyCacheView(image_view);
      next=GetNextImageInList(next);
      if (SyncCacheViewAuthenticPixels(combine_view,exception) == MagickFalse)
        status=MagickFalse;
      if (image->progress_monitor != (MagickProgressMonitor) NULL)
        {
          MagickBooleanType
            proceed;

          proceed=SetImageProgress(image,CombineImageTag,progress++,
            combine_image->rows);
          if (proceed == MagickFalse)
            status=MagickFalse;
        }
    }
  }
  combine_view=DestroyCacheView(combine_view);
  if (status == MagickFalse)
    combine_image=DestroyImage(combine_image);
  return(combine_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     S e p a r a t e I m a g e                                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  SeparateImage() separates a channel from the image and returns it as a
%  grayscale image.
%
%  The format of the SeparateImage method is:
%
%      Image *SeparateImage(const Image *image,const ChannelType channel,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o channel: the image channel.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport Image *SeparateImage(const Image *image,
  const ChannelType channel_type,ExceptionInfo *exception)
{
#define GetChannelBit(mask,bit)  (((size_t) (mask) >> (size_t) (bit)) & 0x01)
#define SeparateImageTag  "Separate/Image"

  CacheView
    *image_view,
    *separate_view;

  Image
    *separate_image;

  MagickBooleanType
    status;

  MagickOffsetType
    progress;

  ssize_t
    y;

  /*
    Initialize spread image attributes.
  */
  assert(image != (Image *) NULL);
  assert(image->signature == MagickSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickSignature);
  separate_image=CloneImage(image,image->columns,image->rows,MagickTrue,
    exception);
  if (separate_image == (Image *) NULL)
    return((Image *) NULL);
  if (SetImageStorageClass(separate_image,DirectClass,exception) == MagickFalse)
    {
      separate_image=DestroyImage(separate_image);
      return((Image *) NULL);
    }
  separate_image->colorspace=GRAYColorspace;
  /*
    Separate image.
  */
  status=MagickTrue;
  progress=0;
  image_view=AcquireCacheView(image);
  separate_view=AcquireCacheView(separate_image);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(progress,status)
#endif
  for (y=0; y < (ssize_t) image->rows; y++)
  {
    register const Quantum
      *restrict p;

    register Quantum
      *restrict q;

    register ssize_t
      x;

    if (status == MagickFalse)
      continue;
    p=GetCacheViewVirtualPixels(image_view,0,y,image->columns,1,exception);
    q=QueueCacheViewAuthenticPixels(separate_view,0,y,separate_image->columns,1,
      exception);
    if ((p == (const Quantum *) NULL) || (q == (Quantum *) NULL))
      {
        status=MagickFalse;
        continue;
      }
    for (x=0; x < (ssize_t) image->columns; x++)
    {
      register ssize_t
        i;

      if (GetPixelMask(image,p) != 0)
        {
          p+=GetPixelChannels(image);
          q+=GetPixelChannels(separate_image);
          continue;
        }
      SetPixelChannel(separate_image,GrayPixelChannel,0,q);
      for (i=0; i < (ssize_t) GetPixelChannels(image); i++)
      {
        PixelChannel
          channel;

        PixelTrait
          traits;

        channel=GetPixelChannelMapChannel(image,i);
        traits=GetPixelChannelMapTraits(image,channel);
        if ((traits == UndefinedPixelTrait) ||
            (GetChannelBit(channel_type,channel) == 0))
          continue;
        SetPixelChannel(separate_image,GrayPixelChannel,p[i],q);
      }
      p+=GetPixelChannels(image);
      q+=GetPixelChannels(separate_image);
    }
    if (SyncCacheViewAuthenticPixels(separate_view,exception) == MagickFalse)
      status=MagickFalse;
    if (image->progress_monitor != (MagickProgressMonitor) NULL)
      {
        MagickBooleanType
          proceed;

#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp critical (MagickCore_SeparateImage)
#endif
        proceed=SetImageProgress(image,SeparateImageTag,progress++,image->rows);
        if (proceed == MagickFalse)
          status=MagickFalse;
      }
  }
  separate_view=DestroyCacheView(separate_view);
  image_view=DestroyCacheView(image_view);
  return(separate_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     S e p a r a t e I m a g e s                                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  SeparateImages() returns a separate grayscale image for each channel
%  specified.
%
%  The format of the SeparateImages method is:
%
%      MagickBooleanType SeparateImages(const Image *image,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport Image *SeparateImages(const Image *image,ExceptionInfo *exception)
{
  Image
    *images,
    *separate_image;

  register ssize_t
    i;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  images=NewImageList();
  for (i=0; i < (ssize_t) GetPixelChannels(image); i++)
  {
    PixelChannel
      channel;

    PixelTrait
      traits;

    channel=GetPixelChannelMapChannel(image,i);
    traits=GetPixelChannelMapTraits(image,channel);
    if ((traits == UndefinedPixelTrait) ||
        ((traits & UpdatePixelTrait) == 0))
      continue;
    separate_image=SeparateImage(image,(ChannelType) (1 << channel),exception);
    if (separate_image != (Image *) NULL)
      AppendImageToList(&images,separate_image);
  }
  return(images);
}