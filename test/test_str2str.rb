require 'inmemory_kv'
require 'minitest/spec'
require 'minitest/autorun'

describe InMemoryKV::Str2Str do
  let(:s2s) { InMemoryKV::Str2Str.new }
  it "should be setable" do
    s2s['asdf'] = 'qwer'
  end
  it "should be fetchable" do
    s2s['asdf'].must_be_nil
  end
  it "should be inspected" do
    s2s.inspect.must_equal '<InMemoryKV::Str2Str>'
  end
  it "is empty" do
    s2s.must_be_empty
  end
  it "has zero size" do
    s2s.size.must_equal 0
  end
  it "has zero data_size" do
    s2s.data_size.must_equal 0
  end
  it "should have nil first" do
    s2s.first.must_be_nil
  end
  it "should have no keys nor values nor entries" do
    s2s.keys.must_be_empty
    s2s.values.must_be_empty
    s2s.entries.must_be_empty
  end
  it "should not iterate" do
    s2s.each_key{ raise }
    s2s.each_value{ raise }
    s2s.each{ raise }
  end

  describe "filled with one item" do
    before do
      s2s['asdf'] = 'qwer'
    end
    it "should be inspectable" do
      s2s.inspect.must_equal '<InMemoryKV::Str2Str "asdf"=>"qwer">'
    end
    it "should be fetchable" do
      s2s['asdf'].must_equal 'qwer'
    end
    it "should has size == 1" do
      s2s.size.must_equal 1
    end
    it "should has nonzero data_size" do
      s2s.data_size.wont_equal 0
    end
    it "first should not be nil" do
      s2s.first.must_equal ["asdf", "qwer"]
    end
    it "should react on delete" do
      s2s.delete("asdf").must_equal('qwer')
      s2s.size.must_equal 0
      s2s.data_size.must_equal 0
      s2s.first.must_be_nil
      s2s.each{ raise }
    end
    it "should react on shift" do
      s2s.shift.must_equal(['asdf','qwer'])
      s2s.size.must_equal 0
      s2s.data_size.must_equal 0
      s2s.first.must_be_nil
      s2s.each{ raise }
    end
    it "should allow rewrite value" do
      s2s['asdf'] = 'zxcv'
      s2s['asdf'].must_equal 'zxcv'
    end
    it "should allow dup, and perform copy on write" do
      copy = s2s.dup
      copy['asdf'].must_equal 'qwer'
      s2s['asdf'] = 'zxcv'
      s2s['asdf'].must_equal 'zxcv'
      copy['asdf'].must_equal 'qwer'
    end
  end

  describe "filled with two items" do
    before do
      s2s['asdf'] = 'qwer'
      s2s['qwer'] = 'zxcv'
    end
    it "should be inspectable" do
      s2s.inspect.must_equal '<InMemoryKV::Str2Str "asdf"=>"qwer" "qwer"=>"zxcv">'
    end
    it "should be fetchable" do
      s2s['asdf'].must_equal 'qwer'
      s2s['qwer'].must_equal 'zxcv'
    end
    it "should has size == 2" do
      s2s.size.must_equal 2
    end
    it "first should not be nil" do
      s2s.first.must_equal ["asdf", "qwer"]
    end
    it "should react on delete" do
      s2s.delete("asdf").must_equal('qwer')
      s2s.size.must_equal 1
      s2s.first.must_equal ['qwer', 'zxcv']
      s2s.delete("qwer").must_equal('zxcv')
      s2s.data_size.must_equal 0
      s2s.each{ raise }
    end
    it "should react on shift" do
      s2s.shift.must_equal(['asdf','qwer'])
      s2s.size.must_equal 1
      s2s.shift.must_equal ['qwer', 'zxcv']
      s2s.size.must_equal 0
      s2s.data_size.must_equal 0
      s2s.first.must_be_nil
      s2s.each{ raise }
    end
    it "should reorder on set" do
      s2s['asdf'] = 'yuio'
      s2s.first.must_equal ['qwer', 'zxcv']
      s2s.entries.must_equal [
        ['qwer', 'zxcv'],
        ['asdf', 'yuio']
      ]
    end
    it "should allow explicit reorder" do
      s2s.up 'asdf'
      s2s.entries.must_equal [
        ['qwer', 'zxcv'],
        ['asdf', 'qwer']
      ]
      s2s.down 'asdf'
      s2s.entries.must_equal [
        ['asdf', 'qwer'],
        ['qwer', 'zxcv']
      ]
    end
    it "should allow dup, and perform copy on write" do
      copy = s2s.dup
      copy['asdf'].must_equal 'qwer'
      s2s['asdf'] = 'zxcv'
      s2s['asdf'].must_equal 'zxcv'
      copy['asdf'].must_equal 'qwer'

      s2s['qwer'].must_equal 'zxcv'
      copy['qwer'].must_equal 'zxcv'
    end
  end

  describe "huge filled" do
    let(:num) { 1000 }
    before do
      num.times do |i|
        s2s[i.to_s] = "q#{i}"
      end
    end
    let(:hsh) do
      h = {}
      num.times do |i|
        h[i.to_s] = "q#{i}"
      end
      h
    end
    it "should store all values" do
      num.times do |i|
        s2s[i.to_s].must_equal "q#{i}"
      end
    end
    it "should report entries" do
      s2s.entries.must_equal hsh.entries
    end
    it "should delete entries" do
      2.step(num, 2) do |i|
        s2s.delete(i.to_s).must_equal hsh.delete(i.to_s)
      end
      1.step(num, 2) do |i|
        s2s[i.to_s].must_equal hsh[i.to_s]
      end
      s2s.size.must_equal hsh.size
      s2s.entries.must_equal hsh.entries
    end
    it "should reorder on set" do
      s2s.first.must_equal ['0', 'q0']
      s2s['0'] = 'ya'
      s2s.first.must_equal ['1', 'q1']
      s2s.entries.last.must_equal ['0', 'ya']
    end
    it "should allow explicit reorder" do
      s2s.up '235'
      s2s.entries.last.must_equal ['235', 'q235']
      s2s.down '235'
      s2s.first.must_equal ['235', 'q235']
      s2s.entries.last.wont_equal ['235', 'q235']
    end
  end
end
