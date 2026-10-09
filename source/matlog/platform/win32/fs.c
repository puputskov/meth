#include <matlog/platform/fs.h>

#include <matlog/core/vec.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

ML_VEC_DEFINE (WideVec, uint16_t)

/* UTF-8 to a NUL terminated UTF-16 string in out. */
static MlResult wide_from_utf8 (WideVec *out, MlSlice s)
{
	MlResult r = ML_OK;
	int32_t  n = 0;

	WideVec_clear (out);

	if (s.len > 0x7FFFFFFF)
	{
		r = ML_ERR_INVALID;
	}

	else if (s.len > 0)
	{
		n = MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, s.data, (int32_t)s.len, NULL, 0);
		r = n > 0 ? ML_OK : ML_ERR_UTF8;
	}

	if (r == ML_OK)
	{
		r = WideVec_reserve (out, (uint64_t)n + 1);
	}

	if (r == ML_OK)
	{
		if (n > 0)
		{
			MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, s.data, (int32_t)s.len, (LPWSTR)out->data, n);
		}

		out->data [n] = 0;
		out->len      = (uint64_t)n + 1;
	}

	return r;
}

int32_t ml_fs_exists (MlSlice path)
{
	int32_t exists = 0;
	WideVec wide;

	WideVec_init (&wide);

	if (wide_from_utf8 (&wide, path) == ML_OK)
	{
		DWORD attr = GetFileAttributesW ((LPCWSTR)wide.data);

		exists = attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
	}

	WideVec_free (&wide);
	return exists;
}

MlResult ml_fs_read_file (MlStr *out, MlSlice path)
{
	MlResult r    = ML_OK;
	WideVec  wide;
	HANDLE   file = INVALID_HANDLE_VALUE;
	uint64_t base = out->len;
	int32_t  more = 1;

	WideVec_init (&wide);
	r = wide_from_utf8 (&wide, path);

	if (r == ML_OK)
	{
		file = CreateFileW ((LPCWSTR)wide.data, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, NULL);
		r = file != INVALID_HANDLE_VALUE ? ML_OK : ML_ERR_IO;
	}

	while (r == ML_OK && more)
	{
		char  buf [16384];
		DWORD got = 0;

		if (!ReadFile (file, buf, sizeof buf, &got, NULL))
		{
			r = ML_ERR_IO;
		}

		else if (got == 0)
		{
			more = 0;
		}

		else
		{
			r = ml_str_append (out, ml_slice (buf, got));
		}
	}

	/* single cleanup stage; a failed read leaves out as it was */
	if (r != ML_OK && out->data != NULL)
	{
		ml_str_erase (out, base, out->len - base);
	}

	if (file != INVALID_HANDLE_VALUE)
	{
		CloseHandle (file);
	}

	WideVec_free (&wide);
	return r;
}

MlResult ml_fs_write_file_atomic (MlSlice path, MlSlice data)
{
	MlResult r    = ML_OK;
	MlStr    tmp_path;
	WideVec  wide_tmp;
	WideVec  wide_dst;
	HANDLE   file = INVALID_HANDLE_VALUE;
	uint64_t pos  = 0;

	ml_str_init (&tmp_path);
	WideVec_init (&wide_tmp);
	WideVec_init (&wide_dst);

	r = ml_str_append (&tmp_path, path);

	if (r == ML_OK)
	{
		r = ml_str_append (&tmp_path, ml_slice_cstr (".tmp"));
	}

	if (r == ML_OK)
	{
		r = wide_from_utf8 (&wide_tmp, ml_str_view (tmp_path));
	}

	if (r == ML_OK)
	{
		r = wide_from_utf8 (&wide_dst, path);
	}

	if (r == ML_OK)
	{
		file = CreateFileW ((LPCWSTR)wide_tmp.data, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, NULL);
		r = file != INVALID_HANDLE_VALUE ? ML_OK : ML_ERR_IO;
	}

	while (r == ML_OK && pos < data.len)
	{
		DWORD written = 0;
		DWORD chunk   = (DWORD)(data.len - pos > 0x100000 ? 0x100000 : data.len - pos);

		if (WriteFile (file, data.data + pos, chunk, &written, NULL) && written > 0)
		{
			pos += written;
		}

		else
		{
			r = ML_ERR_IO;
		}
	}

	if (r == ML_OK && !FlushFileBuffers (file))
	{
		r = ML_ERR_IO;
	}

	if (file != INVALID_HANDLE_VALUE)
	{
		CloseHandle (file);
	}

	if (r == ML_OK && !MoveFileExW ((LPCWSTR)wide_tmp.data, (LPCWSTR)wide_dst.data,
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		r = ML_ERR_IO;
	}

	if (r != ML_OK && wide_tmp.len > 0)
	{
		DeleteFileW ((LPCWSTR)wide_tmp.data);
	}

	WideVec_free (&wide_dst);
	WideVec_free (&wide_tmp);
	ml_str_free (&tmp_path);
	return r;
}

MlResult ml_fs_remove (MlSlice path)
{
	MlResult r = ML_OK;
	WideVec  wide;

	WideVec_init (&wide);
	r = wide_from_utf8 (&wide, path);

	if (r == ML_OK && !DeleteFileW ((LPCWSTR)wide.data))
	{
		r = ML_ERR_IO;
	}

	WideVec_free (&wide);
	return r;
}
