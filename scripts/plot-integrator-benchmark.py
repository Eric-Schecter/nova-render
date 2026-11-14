"""Plot measured GPU results; requires matplotlib only in a validation tool environment."""
import json
import sys
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
source=Path(sys.argv[1] if len(sys.argv)>1 else 'build/validation/integrator-benchmark.json')
output=Path(sys.argv[2] if len(sys.argv)>2 else 'docs/public/benchmarks')
output.mkdir(parents=True,exist_ok=True)
data=json.loads(source.read_text())
names={'CornellBox':'Diffuse Cornell','CornellIndirect':'Occluded light / indirect','CornellGlossy':'Rough metal / reflective caustic'}
colors={'path':'#3b82f6','bidirectional':'#e69527','photon-mapping':'#1c9a7a'}
labels={'path':'PT + MIS','bidirectional':'BDPT','photon-mapping':'Photon mapping'}
fig,axes=plt.subplots(2,3,figsize=(12,6.8),layout='constrained')
for column,scene in enumerate(data):
    for integrator,color in colors.items():
        fixed=sorted([r for r in scene['records'] if r['integrator']==integrator and 'budgetMs' not in r],key=lambda r:r['spp'])
        timed=sorted([r for r in scene['records'] if r['integrator']==integrator and 'budgetMs' in r],key=lambda r:r['ms'])
        axes[0,column].loglog([r['spp'] for r in fixed],[r['relativeMSE'] for r in fixed],'.-',color=color,label=labels[integrator])
        axes[1,column].loglog([r['ms']/1000 for r in timed],[r['floorRelativeMSE'] for r in timed],'.-',color=color,label=labels[integrator])
    ref=scene['reference']
    axes[0,column].axhline(ref['referenceVariance']/ref['mean']**2,color='#999999',ls=':',label='Estimated reference noise')
    axes[1,column].axhline(ref['floorReferenceVariance']/ref['floorMean']**2,color='#999999',ls=':')
    axes[0,column].set_title(names[scene['name']],fontsize=11)
    axes[0,column].set_xlabel('Camera samples / pixel')
    axes[1,column].set_xlabel('Measured elapsed seconds')
    for row in range(2):
        axes[row,column].grid(True,which='both',alpha=.18)
        axes[row,column].tick_params(labelsize=8)
axes[0,0].set_ylabel('Interior linear luminance MSE / mean²')
axes[1,0].set_ylabel('Visible floor MSE / floor mean²')
handles,labels_=axes[0,0].get_legend_handles_labels()
fig.legend(handles,labels_,loc='outside lower center',ncol=4,frameon=False)
fig.suptitle('Cornell integrators · 48×48 · 5 bounces · no denoising\n32768-spp BDPT reference; 8192 photon paths / iteration; 4 camera spp / iteration',fontsize=11)
fig.savefig(output/'integrator-convergence.png',dpi=170)
fig.savefig(output/'integrator-convergence.svg')
print(output/'integrator-convergence.png')
