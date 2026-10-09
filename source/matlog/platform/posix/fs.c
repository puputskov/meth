#define _POSIX_C_SOURCE 200809L

#include <matlog/platform/fs.h>

#include <matlog/core/utf8.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>

/* The path as a C string in out, after checking that it is valid text. */
static MlResult path_cstr (MlStr *out, MlSlice path)
{
	MlResult r = ml_choose (
		path.len > 0 && ml_utf8_valid (path),
		ML_OK,
		ML_ERR_UTF8
	);

	if (r == ML_OK)
	{
		r = ml_str_append (out, path);
	}

	return r;
}

int32_t ml_fs_exists (MlSlice path)
{
	int32_t exists = 0;
	MlStr   p;

	ml_str_init (&p);

	if (path_cstr (&p, path) == ML_OK)
	{
		exists = access (ml_str_cstr (p), F_OK) == 0;
	}

	ml_str_free (&p);
	return exists;
}

MlResult ml_fs_read_file (MlStr *out, MlSlice path)
{
	MlResult r    = ML_OK;
	MlStr    p;
	int32_t  fd   = -1;
	uint64_t base = out->len;
	int32_t  more = 1;

	ml_str_init (&p);
	r = path_cstr (&p, path);

	if (r == ML_OK)
	{
		fd = open (ml_str_cstr (p), O_RDONLY);
		r  = ml_choose (
			fd >= 0,
			ML_OK,
			ML_ERR_IO
		);
	}

	while (r == ML_OK && more)
	{
		char    buf [16384];
		int64_t got = read (fd, buf, sizeof buf);

		if (got < 0)
		{
			r = ML_ERR_IO;
		}

		else if (got == 0)
		{
			more = 0;
		}

		else
		{
			r = ml_str_append (out, ml_slice (buf, (uint64_t)got));
		}
	}

	/* single cleanup stage; a failed read leaves out as it was */
	if (r != ML_OK && out->data != NULL)
	{
		ml_str_erase (out, base, out->len - base);
	}

	if (fd >= 0)
	{
		close (fd);
	}

	ml_str_free (&p);
	return r;
}

MlResult ml_fs_write_file_atomic (MlSlice path, MlSlice data)
{
	MlResult r   = ML_OK;
	MlStr    dst;
	MlStr    tmp;
	int32_t  fd  = -1;
	uint64_t pos = 0;

	ml_str_init (&dst);
	ml_str_init (&tmp);
	r = path_cstr (&dst, path);

	if (r == ML_OK)
	{
		r = ml_str_append (&tmp, path);
	}

	if (r == ML_OK)
	{
		r = ml_str_append (&tmp, ml_slice_cstr (".tmp"));
	}

	if (r == ML_OK)
	{
		fd = open (ml_str_cstr (tmp), O_WRONLY | O_CREAT | O_TRUNC, 0644);
		r  = ml_choose (
			fd >= 0,
			ML_OK,
			ML_ERR_IO
		);
	}

	while (r == ML_OK && pos < data.len)
	{
		int64_t put = write (fd, data.data + pos, data.len - pos);

		if (put > 0)
		{
			pos += (uint64_t)put;
		}

		else
		{
			r = ML_ERR_IO;
		}
	}

	if (r == ML_OK && fsync (fd) != 0)
	{
		r = ML_ERR_IO;
	}

	if (fd >= 0 && close (fd) != 0 && r == ML_OK)
	{
		r = ML_ERR_IO;
	}

	if (r == ML_OK && rename (ml_str_cstr (tmp), ml_str_cstr (dst)) != 0)
	{
		r = ML_ERR_IO;
	}

	/* single cleanup stage; a failed write leaves no temporary file */
	if (r != ML_OK && fd >= 0)
	{
		unlink (ml_str_cstr (tmp));
	}

	ml_str_free (&tmp);
	ml_str_free (&dst);
	return r;
}

MlResult ml_fs_remove (MlSlice path)
{
	MlStr    p;
	MlResult r;

	ml_str_init (&p);
	r = path_cstr (&p, path);

	if (r == ML_OK && unlink (ml_str_cstr (p)) != 0)
	{
		r = ML_ERR_IO;
	}

	ml_str_free (&p);
	return r;
}

MlResult ml_fs_make_dirs (MlSlice path)
{
	MlResult r = ML_OK;
	MlStr    p;

	ml_str_init (&p);
	r = path_cstr (&p, path);

	for (uint64_t end = 1; end <= path.len && r == ML_OK; end++)
	{
		if (end == path.len || p.data [end] == '/')
		{
			char saved = p.data [end];

			p.data [end] = '\0';

			if (mkdir (p.data, 0777) != 0 && errno != EEXIST)
			{
				r = ML_ERR_IO;
			}

			p.data [end] = saved;
		}
	}

	ml_str_free (&p);
	return r;
}
