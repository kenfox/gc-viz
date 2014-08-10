
val standings = scala.io.Source.fromFile(new java.io.File("data/dkp.log-big"))
 .getLines()
 .map(line => {
   val Array(amount, person, thing) = line.split(',')
   (amount.toInt, person, thing)
 })
 .toSeq
 .groupBy(trans => trans._2)
 .toSeq
 .map(person => {
   person._1 -> person._2.foldLeft(0)((sum, trans) => sum + trans._1)
 })
 .sortWith((a, b) => b._2 < a._2)

println(standings)
