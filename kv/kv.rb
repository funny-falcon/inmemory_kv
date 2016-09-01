require 'bin_utils'
require 'objspace' rescue nil

class String
  unless method_defined?(:b)
    BINARY = 'binary'.freeze
    def b
      dup.force_encoding(BINARY)
    end
  end
end

class MemHash
  NOTHING = Object.new.freeze
  DONT_FIT = Object.new.freeze
  X00 = "\x00".force_encoding('binary').freeze
  XFF4 = ("\xff".force_encoding('binary') * 4).freeze
  BFF4 = ::BinUtils.get_int32_le(XFF4)
  class Chunk < String
    def self.create(ceil)
      ::BinUtils.append_int32_le!(new, ceil, 0, 0)
    end

    def self.copy(oth)
      s = new(X00 * oth.bytesize)
      s[0..-1] = oth
      s
    end

    def ceil
      ::BinUtils.get_int32_le(self)
    end

    def add_kv(key, value)
      kvsize = key.bytesize + value.bytesize
      mask = ceil - 1
      capa = (kvsize + 8 + mask) & (~mask)
      add = ::BinUtils.append_int32_le!(nil, capa, key.bytesize)
      add << key
      ::BinUtils.append_int32_le!(add, value.bytesize)
      add << value << (X00 * (capa - kvsize - 8 + 4))
      pos = bytesize - 4
      self[pos, 4] = add
      pos
    end

    def match_key(pos, key)
      keysize = ::BinUtils.get_int32_le(self, pos + 4)
      if keysize == key.bytesize && key == self[pos + 8, keysize]
        keysize
      else
        nil
      end
    end

    def fetch_kv(pos, key)
      if (keysize = match_key(pos, key))
        valsize = ::BinUtils.get_int32_le(self, pos + 8 + keysize)
        self[pos + 12 + keysize, valsize]
      else
        NOTHING
      end
    end

    def set_kv(pos, key, value)
      if (keysize = match_key(pos, key))
        capa = ::BinUtils.get_int32_le(self, pos)
        valsize = value.bytesize
        mask = ceil - 1
        kvs = keysize + valsize + 8
        kvs_capa = (kvs + mask) & (~mask)
        if kvs <= capa && kvs_capa * 4 > capa * 3
          vs = ::BinUtils.append_int32_le!(nil, valsize)
          vpos = pos + 8 + keysize
          self[vpos, 4] = vs
          self[vpos + 4, valsize] = value
          nil
        else
          DONT_FIT
        end
      else
        NOTHING
      end
    end

    def delete_kv(pos, key)
      if (keysize = match_key(pos, key))
        valsize = ::BinUtils.get_int32_le(self, pos + 8 + keysize)
        val = self[pos + 12 + keysize, valsize]
        delete_pos(pos)
        val
      else
        NOTHING
      end
    end

    def delete_pos(pos)
      capa = ::BinUtils.get_int32_le(self, pos)
      self[pos + 4, 4] = XFF4
      self.deleted += capa + 4
    end

    def deleted
      ::BinUtils.get_int32_le(self, 4)
    end

    def deleted=(v)
      self[4, 4] = ::BinUtils.append_int32_le!(nil, v)
    end

    def each
      pos = 4
      while pos < bytesize - 4
        capa = ::BinUtils.get_int32_le(self, pos)
        keysize = ::BinUtils.get_int32_le(self, pos + 4)
        if keysize != BFF4
          valsize = ::BinUtils.get_int32_le(self, pos + 4 + keysize)
          yield self[pos + 8, keysize], self[pos + 12 + keysize, valsize]
        end
        pos += capa + 4
      end
    end

    def each_pos_k
      pos = 4
      while pos < bytesize - 4
        capa = ::BinUtils.get_int32_le(self, pos)
        keysize = ::BinUtils.get_int32_le(self, pos + 4)
        if keysize != BFF4
          valsize = ::BinUtils.get_int32_le(self, pos + 4 + keysize)
          yield pos, self[pos + 8, keysize]
        end
        pos += capa + 4
      end
    end

    def need_gc?
      bytesize * 3 < deleted * 4
    end
  end

  CHUNK_LIMIT = 1024 * 1024 * 4
  def initialize
    @nom = 0
    @chunks = {0 => Chunk.create(16)}
    @capa = 16
    @count = 0
    @occu = 0
    @keys = X00 * (@capa * 8)
  end

  def []=(k, v)
    h = k.b.hash
    bh = (h >> 25) & 0x7f
    m = @capa - 1
    p = h & m
    free = nil
    d = ((h % (@capa / 2)) * 2) | 1 
    while true
      p8 = p * 8
      pos = ::BinUtils.get_int24_le(@keys, p8 + 1)
      flags = @keys.getbyte(p8)
      ph = flags & 0x7f
      collision = (flags >> 7) == 1
      if pos == 0
        free = p unless free
        break unless collision
      elsif ph == bh
        chnom = ::BinUtils.get_int32_le(@keys, p8 + 4)
        chunk = @chunks[chnom]
        case chunk.set_kv(pos, k, v)
        when nil
          return v
        when NOTHING
          flags = (1 << 7) | ph
          @keys.setbyte(p8, flags)
        when DONT_FIT
          _delete_from_chunk(chnom, chunk, pos)
          free = p
          break
        end
      else
        flags = (1 << 7) | ph
        @keys.setbyte(p8, flags)
      end
      p = (p + d) & m
    end
    p8 = p * 8
    pos = ::BinUtils.get_int24_le(@keys, p8 + 1)
    flags = @keys.getbyte(p8)
    if pos == 0
      @count += 1
      @occu += 1 if (flags & 0x80) == 0
    end
    chnom = @nom
    pos = _add_to_chunk(k, v)
    v1 = bh | (flags & 0x80) | (pos << 8)
    add = ::BinUtils.append_int32_le!(nil, v1, chnom)
    @keys[p8, 8] = add
    _rehash! if @occu * 4 > @capa * 3
  end

  def fetch(k, default = NOTHING, &block)
    h = k.b.hash
    bh = (h >> 25) & 0x7f
    m = @capa - 1
    p = h & m
    d = ((h % (@capa / 2)) * 2) | 1 
    while true
      p8 = p * 8
      pos = ::BinUtils.get_int24_le(@keys, p8 + 1)
      flags = @keys.getbyte(p8)
      ph = flags & 0x7f
      collision = (flags >> 7) == 1
      if ph == bh
        chnom = ::BinUtils.get_int32_le(@keys, p8 + 4)
        chunk = @chunks[chnom]
        if (val = chunk.fetch_kv(pos, k)) != NOTHING
          return val
        end
      end
      unless collision
        if default != NOTHING
          return default
        elsif block
          return block[k]
        else
          raise KeyError, k
        end
      end
      p = (p + d) & m
    end
  end

  def [](k)
    fetch(k, nil)
  end

  def _rehash!
    @capa *= 2
    m = @capa - 1
    nkeys = X00 * (@capa * 8)
    @keys.clear
    @chunks.each do |chnom, chunk|
      chunk.each_pos_k do |pos, k|
        h = k.hash
        bh = (h >> 25) & 0x7f
        p = h & m
        d = ((h % (@capa / 2)) * 2) | 1 
        while true
          p8 = p * 8
          ppos = ::BinUtils.get_int24_le(nkeys, p8 + 1)
          if ppos == 0
            v1 = bh | (pos << 8)
            add = ::BinUtils.append_int32_le!(nil, v1, chnom)
            nkeys[p8, 8] = add
            break
          end
          flags = nkeys.getbyte(p8) | (1 << 7)
          nkeys.setbyte(p8, flags)
          p = (p + d) & m
        end
      end
    end
    @keys = nkeys
    @occu = @count
  end

  def _delete_from_chunk(chnom, chunk, pos)
    chunk.delete_pos(pos)
    if chunk.need_gc?
      @chunks.delete(chnom)
      chunk.each do |k, v|
        cnom = @nom
        pos = _add_to_chunk(cnom, k, v)
        h = k.hash
        bh = (h >> 25) & 0x7f
        m = @capa - 1
        p = h & m
        free = nil
        d = ((h % (@capa / 2)) * 2) | 1 
        while true
          p8 = p * 8
          ppos = ::BinUtils.get_int24_le(@keys, p8 + 1)
          pchnom = ::BinUtils.get_int32_le(@keys, p8 + 4)
          if pos == ppos && chnom == pchnom
            set = ::BinUtils.append_int24_int32_le!(nil, pos, cnom)
            @keys[p8+1, 7] = set
            break
          end
          p = (p + d) & m
        end
      end
    end
  end

  def _add_to_chunk(k, v)
    chunk = @chunks[@nom]
    pos = chunk.add_kv(k, v)
    if chunk.bytesize > CHUNK_LIMIT
      nch = Chunk.copy(chunk)
      @chunks[@nom] = nch
      chunk.clear
      @nom += 1
      @chunks[@nom] = Chunk.create(16)
    end
    pos
  end
end
