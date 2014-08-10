require 'json'

# This program is almost identical to dkp.rb, but because
# a large temporary result is assigned to a variable, it
# becomes a live GC root and prevents it from becoming
# garbage.

# Functional style avoids temporary variables partly to
# avoid this unnecessary capture. Garbage is generated like
# crazy in functional style, but it usually performs fine
# due to good GC algorithms that scale with live data.

dkp_log = File.foreach("data/dkp.log-big").map { |line|
  amount, person, thing = line.strip.split(",")
  [ amount.to_i, person, thing ]
}

standings = dkp_log.group_by { |trans| trans[1] }.map { |person, history|
  [ person, history.reduce(0) { |sum, trans| sum + trans[0] } ]
}.sort { |a, b| b[1] <=> a[1] }

puts standings.to_json
