# Ensure filesdir exists
if [ -z $1 ]
then
    echo "filesdir must be specified"
    exit 1
else
    filesdir=$1
fi

# Ensure searchstr exists
if [ -z $2 ]
then
    echo "searchstr must be specified"
    exit 1
else
    searchstr=$2
fi

if [ -d ${filesdir} ]
then
    # Find all files in filesdir, then count them
    num_files=$(find ${filesdir} -type f | wc -l)

    # Count the number of lines matching searchstr
    num_lines=$(grep -r ${searchstr} ${filesdir} | wc -l)
    echo "The number of files are ${num_files} and the number of matching lines are ${num_lines}"
else
    echo "filesdir must be a valid directory"
    exit 1
fi
