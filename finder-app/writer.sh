# Ensure writefile exists
if [ -z $1 ]
then
    echo "writefile must be provided"
    exit 1
else
    writefile=$1
fi

# Ensure writestr exists
if [ -z $2 ]
then
    echo "writestr must be provided"
    exit 1
else
    writestr=$2
fi

if [ ! -e ${writefile} ] || [ -w ${writefile} ]
then
    # Create directory if it doesn't exist
    dir=$(dirname ${writefile})
    mkdir -p ${dir}

    # Create file with content
    echo ${writestr} > ${writefile}
    echo "Created file ${writefile}"
else
    echo "writefile is not writable"
    exit 1
fi
