#include "ins.h"
#include "file_source.h"

static const int BUFFER_SIZE = (1 << 21);
static const int AVG_READ_SIZE = (BUFFER_SIZE >> 1);

file_source::file_source()
   : m_file(NULL)
   , m_circle_buffer(NULL)
   , m_buffer_size(BUFFER_SIZE)
   , m_file_size(0)
   , m_offset(0)
   , m_write_p(0)
   , m_read_p(0)
{

}

file_source::~file_source()
{
   close();
}

bool file_source::open(void* ctx)
{
   // 保存ctx.
   m_open_data.reset((open_file_data*)ctx);

   // 打开文件.
   if (!boost::filesystem2::exists(m_open_data->filename))
      return false;

   // 创建缓冲.
   m_circle_buffer = new char[m_buffer_size];
   if (!m_circle_buffer)
      return false;

   // 重置指针.
   m_write_p = m_read_p = m_offset = 0;

   // 获得文件大小.
   m_file_size = boost::filesystem2::file_size(m_open_data->filename);

   // 打开文件.
   m_file = fopen(m_open_data->filename.c_str(), "rb");
   if (!m_file)
      return false;

   return true;
}

bool file_source::read_data(char* data, boost::uint64_t offset, boost::uint64_t size, boost::uint64_t& read_size)
{
   static char read_buffer[AVG_READ_SIZE];
   // 根据参数自动加锁.
   boost::shared_ptr<boost::mutex::scoped_lock> lock;
   if (m_open_data->is_multithread)
      lock.reset(new boost::mutex::scoped_lock(m_mutex));

   read_size = 0;

   // 读取数据越界.
   if (offset >= m_file_size)
      return false;

   // 如果数据读取位置在缓冲范围中, 则直接从缓冲中拉取数据.
   if (m_offset <= offset && offset < m_offset + (m_write_p - m_read_p))
   {
      // 计算出位置.
      unsigned int p = offset - m_offset;
      m_read_p += p;
      // 从缓冲中读取数据.
      read_size = get_data(data, size);
      // 更新偏移位置.
      m_offset = offset + read_size;
   }
   else
   {
      // 从文件中读取数据.
      m_offset = offset;
      m_write_p = m_read_p = 0;

      // 移到偏移位置.
      fseek(m_file, offset, SEEK_SET);
      // 开始读取数据.
      int r = fread(m_circle_buffer, 1, AVG_READ_SIZE, m_file);
      m_write_p += r;
      // 复制数据到读取buf.
      size = _min(AVG_READ_SIZE, size);
      memcpy(data, m_circle_buffer, size);
      // 更新返回数据大小.
      read_size = size;
   }

   // 如果小于缓冲长度的一半, 则从文件读取一半缓冲长度的数据
   // 到缓冲.
   offset = m_offset + (m_write_p - m_read_p);
   if (available_size() < AVG_READ_SIZE &&
      offset < m_file_size)
   {
      fseek(m_file, offset, SEEK_SET);
      int r = fread(read_buffer, 1, AVG_READ_SIZE, m_file);
      if (r > 0)
         put_data(read_buffer, r);
   }

   return true;
}

void file_source::close()
{
   if (m_file)
   {
      fclose(m_file);
      m_file = NULL;
   }

   if (m_circle_buffer)
   {
      delete[] m_circle_buffer;
      m_circle_buffer = NULL;
   }

   m_write_p = m_read_p = m_offset = 0;
}

unsigned int file_source::put_data(char* buffer, unsigned int len)
{
   unsigned int l;
   len = _min(len, m_buffer_size - m_write_p + m_read_p);
   /* first put the data starting from fifo->in to buffer end */
   l = _min(len, m_buffer_size - (m_write_p & (m_buffer_size - 1)));
   memcpy(m_circle_buffer + (m_write_p & (m_buffer_size - 1)), buffer, l);
   /* then put the rest (if any) at the beginning of the buffer */
   memcpy(m_circle_buffer, buffer + l, len - l);
   m_write_p += len;
   return len;
}

unsigned int file_source::get_data(char* buffer, unsigned int len)
{
   unsigned int l; 
   len = _min(len, m_write_p - m_read_p); 
   /* first get the data from fifo->out until the end of the buffer */ 
   l = _min(len, m_buffer_size - (m_read_p & (m_buffer_size - 1))); 
   memcpy(buffer, m_circle_buffer + (m_read_p & (m_buffer_size - 1)), l); 
   /* then get the rest (if any) from the beginning of the buffer */ 
   memcpy(buffer + l, m_circle_buffer, len - l); 
   m_read_p += len; 
   return len; 
}

inline unsigned int file_source::_max(unsigned int a, unsigned int b)
{
   return max(a, b);
}

inline unsigned int file_source::_min(unsigned int a, unsigned int b)
{
   return min(a, b);
}

