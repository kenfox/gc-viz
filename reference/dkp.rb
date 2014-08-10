require 'json'

def dkp_log
  File.foreach("data/dkp.log-big").map { |line|
    amount, person, thing = line.strip.split(",")
    [ amount.to_i, person, thing ]
  }
end

standings = dkp_log.group_by { |trans| trans[1] }.map { |person, history|
  [ person, history.reduce(0) { |sum, trans| sum + trans[0] } ]
}.sort { |a, b| b[1] <=> a[1] }

puts standings.to_json
