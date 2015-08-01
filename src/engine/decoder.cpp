 /* fre:ac - free audio converter
  * Copyright (C) 2001-2015 Robert Kausch <robert.kausch@freac.org>
  *
  * This program is free software; you can redistribute it and/or
  * modify it under the terms of the "GNU General Public License".
  *
  * THIS PACKAGE IS PROVIDED "AS IS" AND WITHOUT ANY EXPRESS OR
  * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
  * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE. */

#include <engine/decoder.h>

#include <smooth/io/drivers/driver_zero.h>

using namespace smooth::IO;

using namespace BoCA;
using namespace BoCA::AS;

Array<Threads::Mutex *>	 BonkEnc::Decoder::mutexes;
Threads::Mutex		 BonkEnc::Decoder::managementMutex;

BonkEnc::Decoder::Decoder(const BoCA::Config *iConfiguration)
{
	configuration = iConfiguration;

	stream	      = NIL;
	decoder	      = NIL;

	calculateMD5  = False;

	sampleOffset  = 0;
}

BonkEnc::Decoder::~Decoder()
{
	Destroy();
}

Bool BonkEnc::Decoder::Create(const String &nFileName, const Track &track)
{
	static DriverZero	 zero_in;

	Registry	&boca	= Registry::Get();
	const Format	&format = track.GetFormat();

	if (nFileName.StartsWith("device://")) stream = new InStream(STREAM_DRIVER, &zero_in);
	else				       stream = new InStream(STREAM_FILE, nFileName, IS_READ);

	stream->SetPackageSize((track.length >= 0 ? 32768 : 2048) * format.channels * (format.bits / 8));

	if (stream->GetLastError() != IO_ERROR_OK)
	{
		BoCA::Utilities::ErrorMessage("Cannot access input file: %1", nFileName);

		delete stream;
		stream = NIL;

		return False;
	}

	/* Create decoder component.
	 */
	decoder = boca.CreateDecoderForStream(nFileName);

	if (decoder == NIL)
	{
		BoCA::Utilities::ErrorMessage("Cannot create decoder component for input file: %1", nFileName);

		delete stream;
		stream = NIL;

		return False;
	}

	/* Lock decoder if it's not thread safe.
	 */
	if (!decoder->IsThreadSafe())
	{
		managementMutex.Lock();

		if (mutexes.Get(decoder->GetID().ComputeCRC32()) == NIL) mutexes.Add(new Threads::Mutex(), decoder->GetID().ComputeCRC32());

		managementMutex.Release();

		mutexes.Get(decoder->GetID().ComputeCRC32())->Lock();
	}

	/* Add decoder to stream.
	 */
	Track	 trackInfo = track;

	trackInfo.origFilename = nFileName;

	decoder->SetConfiguration(configuration);
	decoder->SetAudioTrackInfo(trackInfo);

	if (stream->AddFilter(decoder) == False)
	{
		BoCA::Utilities::ErrorMessage("Cannot set up decoder for input file: %1\n\nError: %2", File(nFileName).GetFileName(), decoder->GetErrorString());

		if (!decoder->IsThreadSafe()) mutexes.Get(decoder->GetID().ComputeCRC32())->Release();

		boca.DeleteComponent(decoder);

		decoder = NIL;

		delete stream;
		stream = NIL;

		return False;
	}

	/* Seek to sampleOffset if necessary.
	 */
	if (track.sampleOffset > 0 && !decoder->Seek(track.sampleOffset))
	{
		Int64			 bytesLeft = track.sampleOffset * format.channels * (format.bits / 8);
		Buffer<UnsignedByte>	 buffer;

		while (bytesLeft)
		{
			buffer.Resize(Math::Min((Int64) 1024, bytesLeft));

			bytesLeft -= Read(buffer);
		}
	}

	fileName     = nFileName;
	sampleOffset = track.sampleOffset;

	return True;
}

Bool BonkEnc::Decoder::Destroy()
{
	if (decoder == NIL || stream == NIL) return False;

	Registry	&boca = Registry::Get();

	stream->RemoveFilter(decoder);

	if (decoder->GetErrorState()) BoCA::Utilities::ErrorMessage("Error: %1", decoder->GetErrorString());

	if (!decoder->IsThreadSafe()) mutexes.Get(decoder->GetID().ComputeCRC32())->Release();

	boca.DeleteComponent(decoder);

	delete stream;

	decoder	     = NIL;
	stream	     = NIL;

	fileName     = NIL;
	sampleOffset = 0;

	return True;
}

Bool BonkEnc::Decoder::GetStreamInfo(Track &track)
{
	return decoder->GetStreamInfo(fileName, track);
}

Int BonkEnc::Decoder::Read(Buffer<UnsignedByte> &buffer)
{
	if (decoder == NIL || stream == NIL) return 0;

	Int	 bytes = stream->InputData(buffer, buffer.Size());

	if (bytes >= 0)
	{
		buffer.Resize(bytes);

		if (calculateMD5) md5.Feed(buffer);
	}

	return bytes;
}

Bool BonkEnc::Decoder::Seek(Int64 sample)
{
	return decoder->Seek(sampleOffset + sample);
}

Int64 BonkEnc::Decoder::GetInBytes() const
{
	return decoder->GetInBytes();
}

String BonkEnc::Decoder::GetDecoderName() const
{
	return decoder->GetName();
}

String BonkEnc::Decoder::GetMD5Checksum()
{
	return md5.Finish();
}

Void BonkEnc::Decoder::FreeLockObjects()
{
	foreach (Threads::Mutex *mutex, mutexes) delete mutex;

	mutexes.RemoveAll();
}