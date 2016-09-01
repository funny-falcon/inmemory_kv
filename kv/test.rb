require './kv'

if false
kv = MemHash.new
kv['a'] = 'b'
p kv['a']

kk = 'qwertyuiopasdfghjklzxcvbnm'
(1..(kk.size)).each do |sz|
  kv = MemHash.new
  sz.times{|i| c = kk[i]; kv[c] = c+'!'}
  r1, r2 = '', ''
  sz.times{|i| c = kk[i]; r1 << kv[c]; r2 << (c+'!')}
  p [sz, r1==r2]
end
end

n, k = ARGV
n = n.to_i
store = k == 'h' ? {} : MemHash.new
start = Time.new.to_f
(1..n).each do |i|
  store[::BinUtils.append_int64_le!(nil, i)] = ("!!!!%d!!!!" % i) * 4
  if i % 10000 == 0
    print format("%d\t%.2f\n", i, Time.now.to_f - start)
  end
end
print format("%d\t%.2f\n", n, Time.now.to_f - start)
